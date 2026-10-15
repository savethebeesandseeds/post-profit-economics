// SPDX-License-Identifier: MIT
// Acceptance against the SUPPLIED Windows IdyDB library. Never build/link this
// with a fake database. These tests are pending until that dependency arrives.
#include "storage/environment_store.hpp"
#include "platform/windows_files.hpp"
#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
namespace win = ph::exchange::platform::windows;
using namespace ph::exchange::storage;
namespace {
int checks = 0;
void check(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation, const char* message) {
  bool rejected = false;
  try { operation(); } catch (const std::exception&) { rejected = true; }
  check(rejected, message);
}
void directory(const fs::path& path) {
  win::PrivateSecurity security;
  if (!CreateDirectoryW(win::native_path(path).c_str(), security.get()))
    win::fail("cannot create private storage test directory");
}
void write_fixture(const fs::path& path, const std::string& bytes) {
  win::PrivateSecurity security;
  win::Handle handle(CreateFileW(win::native_path(path).c_str(), GENERIC_WRITE,
      0, security.get(), CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!handle) win::fail("cannot create storage test fixture");
  DWORD written = 0;
  if (!WriteFile(handle.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
      written != bytes.size()) win::fail("cannot write storage test fixture");
}
std::string bytes(const fs::path& path) {
  const win::PinnedPath pinned(path, true);
  const auto handle = win::open_regular(pinned, GENERIC_READ, FILE_SHARE_READ, 128 * 1024, true);
  std::string result;
  char buffer[4096];
  for (;;) {
    DWORD count = 0;
    if (!ReadFile(handle.get(), buffer, sizeof(buffer), &count, nullptr))
      win::fail("cannot read stored test ciphertext");
    if (!count) break;
    result.append(buffer, count);
    check(result.size() <= 128 * 1024, "bounded ciphertext read");
  }
  return result;
}
}  // namespace

int main() {
  fs::path root;
  try {
    wchar_t temporary[32768];
    const DWORD size = GetTempPathW(32768, temporary);
    check(size && size < 32768, "temporary directory is available");
    root = win::local_path(fs::path(temporary) /
        (L"exchange-idydb-acceptance-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64())));
    directory(root);
    const auto store = root / L"donn\u00e9es.idydb";
    const std::string key = "test-only-not-an-operational-key";
    const std::string original =
        R"({"revision":0,"cash_minor":9223372036854775807,"decimal":1.00000000000000001,"text":"caf\u00e9"})";
    const std::string next = R"({"revision":1,"cash_minor":9223372036854775806})";
    create_environment_store(store, original, key);
    const auto saved = bytes(store);
    check(saved.substr(0, 8) == "IDYDBENC", "saved bytes use native IdyDB encryption");
    check(saved.find("cash_minor") == std::string::npos, "ciphertext does not contain account text");
    check(read_environment_store(store, key) == original, "exact account text and integers reopen");
    check(bytes(store) == saved, "read-only open preserves ciphertext exactly");
    rejects([&] { create_environment_store(store, next, key); }, "creation never replaces existing records");
    rejects([&] { read_environment_store(store, "wrong-key"); }, "wrong key is rejected");
    rejects([&] { read_environment_store(store); }, "missing key is rejected");
    rejects([&] { replace_environment_store(store, original, next, "wrong-key"); },
            "wrong-key replacement is rejected");
    rejects([&] { replace_environment_store(store, original, next); },
            "missing-key replacement is rejected");
    check(bytes(store) == saved, "rejected operations preserve ciphertext exactly");

    const auto missing = root / L"missing.idydb";
    rejects([&] { read_environment_store(missing, key); }, "read never creates a database");
    rejects([&] { replace_environment_store(missing, original, next, key); },
            "replace never initializes a missing database");
    check(!fs::exists(missing), "missing database remains absent");
    for (const auto& invalid : {std::string(), std::string(environment_payload_limit + 1, 'x'),
                               std::string("one\0two", 7)}) {
      rejects([&] { create_environment_store(missing, invalid, key); }, "invalid payload rejected");
      check(!fs::exists(missing), "invalid payload creates no database");
    }
    rejects([&] { create_environment_store(missing, original); }, "Windows create requires encryption");
    rejects([&] { create_environment_store(missing, original, std::string("one\0two", 7)); },
            "embedded-NUL key rejected");
    check(!fs::exists(missing), "invalid key creates no database");

    const auto plain = root / L"plain.idydb";
    const std::string plain_bytes = "{\"this\":\"is not encrypted\"}";
    write_fixture(plain, plain_bytes);
    rejects([&] { read_environment_store(plain, key); }, "plaintext read is rejected");
    rejects([&] { replace_environment_store(plain, original, next, key); },
            "plaintext is never implicitly migrated by writable open");
    check(bytes(plain) == plain_bytes, "plaintext evidence is preserved");

    std::string damaged = saved;
    damaged.back() ^= 1;
    const auto tampered = root / L"tampered.idydb";
    write_fixture(tampered, damaged);
    rejects([&] { read_environment_store(tampered, key); }, "tampered ciphertext is rejected");
    rejects([&] { replace_environment_store(tampered, original, next, key); },
            "tampered ciphertext cannot be updated");
    check(bytes(tampered) == damaged, "tampered evidence is preserved");

    replace_environment_store(store, original, next, key);
    check(read_environment_store(store, key) == next, "encrypted update reopens exactly");
    const auto updated = bytes(store);
    bool conflict = false;
    try { replace_environment_store(store, original, next, key); }
    catch (const StoreConflict&) { conflict = true; }
    check(conflict, "stale revision has distinct StoreConflict outcome");
    check(bytes(store) == updated, "stale write preserves exact ciphertext");

    const std::string proposals[] = {R"({"revision":2,"winner":1})", R"({"revision":2,"winner":2})"};
    std::atomic<int> ready {0}, wins {0}, conflicts {0};
    std::atomic<bool> start {false};
    std::exception_ptr failures[2];
    std::thread workers[2];
    for (int i = 0; i != 2; ++i) workers[i] = std::thread([&, i] {
      ++ready;
      while (!start) std::this_thread::yield();
      try {
        replace_environment_store(store, next, proposals[i], key);
        ++wins;
      } catch (const StoreConflict&) { ++conflicts; }
      catch (...) { failures[i] = std::current_exception(); }
    });
    while (ready != 2) std::this_thread::yield();
    start = true;
    for (auto& worker : workers) worker.join();
    for (const auto& error : failures) if (error) std::rethrow_exception(error);
    check(wins == 1 && conflicts == 1, "one same-revision writer commits and one conflicts");
    const auto winner = read_environment_store(store, key);
    check(winner == proposals[0] || winner == proposals[1], "winning envelope reopens intact");

    const auto maximum = root / L"maximum.idydb";
    const std::string at_limit(environment_payload_limit, 'x');
    create_environment_store(maximum, at_limit, key);
    check(read_environment_store(maximum, key) == at_limit, "maximum envelope reopens exactly");
    check(fs::file_size(maximum) <= 128 * 1024, "maximum envelope remains within ciphertext file bound");

    std::cout << "Windows encrypted storage: " << checks << " checks passed.\n";
  } catch (const std::exception& error) {
    std::cerr << "Windows encrypted storage: " << error.what()
              << "\nIsolated acceptance fixture retained for inspection.\n";
    return 1;
  }
  fs::remove_all(root);
  return 0;
}
