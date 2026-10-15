// SPDX-License-Identifier: MIT
#include "storage/environment_store.hpp"
#include "platform/windows_files.hpp"
#include "storage/windows_lock.hpp"
#include <db.h>

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ph::exchange::storage {
namespace {
namespace fs = std::filesystem;
namespace win = ph::exchange::platform::windows;
constexpr std::uint64_t file_size_limit = 128 * 1024;

void validate_key(const std::string& passphrase) {
  if (passphrase.empty() || passphrase.find('\0') != std::string::npos)
    throw std::runtime_error("Windows environment storage requires a nonempty encryption key without NUL");
}
void validate_payload(const std::string& payload) {
  if (payload.empty() || payload.size() > environment_payload_limit ||
      payload.find('\0') != std::string::npos)
    throw std::runtime_error("environment database payload must contain 1 to " +
                             std::to_string(environment_payload_limit) + " bytes without NUL");
}

using Lock = windows::StableLock;

win::Handle checked_ciphertext(const win::PinnedPath& location) {
  auto handle = win::open_regular(location, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, file_size_limit);
  win::require_private_acl(handle.get(), true);
  std::array<char, 8> magic {};
  DWORD count = 0;
  if (!ReadFile(handle.get(), magic.data(), static_cast<DWORD>(magic.size()), &count, nullptr))
    win::fail("cannot inspect encrypted Windows environment storage");
  if (count != magic.size() || std::memcmp(magic.data(), "IDYDBENC", magic.size()) != 0)
    throw std::runtime_error("Windows environment requires native IdyDB ciphertext; no plaintext fallback");
  return handle;
}

class Database {
 public:
  Database() = default;
  ~Database() {
    // Never retry a failed encrypted close while unwinding. The library owns
    // decrypted working storage and must clear/discard it on every exit path.
    if (handle_) idydb_discard(&handle_);
  }
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  void open(const fs::path& path, int flags, const std::string& passphrase) {
    const auto filename = win::utf8_path(path);
    expect(idydb_open_encrypted(filename.c_str(), &handle_, flags, passphrase.c_str()),
           IDYDB_SUCCESS, "encrypted open");
  }
  void expect(int actual, int expected, const char* operation) {
    if (actual != expected)
      throw std::runtime_error(std::string("environment database ") + operation +
          " failed (IdyDB status " + std::to_string(actual) + ")");
  }
  std::string extract() {
    expect(idydb_extract(&handle_, 1, 1), IDYDB_DONE, "extract envelope");
    if (idydb_retrieved_type(&handle_) != IDYDB_CHAR)
      throw std::runtime_error("environment database envelope must be an IdyDB text cell");
    const char* text = idydb_retrieve_char(&handle_);
    if (!text) throw std::runtime_error("environment database envelope text is unavailable");
    std::size_t size = 0;
    while (size <= environment_payload_limit && text[size] != '\0') ++size;
    if (!size || size > environment_payload_limit)
      throw std::runtime_error("environment database envelope exceeds the payload bound");
    return std::string(text, size);
  }
  void insert(const std::string& payload) {
    expect(idydb_insert_const_char(&handle_, 1, 1, payload.c_str()), IDYDB_DONE, "insert envelope");
  }
  void close() { expect(idydb_close(&handle_), IDYDB_DONE, "close"); }
 private:
  idydb* handle_ = nullptr;
};

void verify_same_file(const win::PinnedPath& location, HANDLE previous) {
  const auto named = win::open_regular(location, FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, file_size_limit);
  win::require_private_acl(named.get(), true);
  if (win::identity(previous) != win::identity(named.get()))
    throw std::runtime_error("environment database identity changed during open");
}

std::string read_locked(const win::PinnedPath& location, const std::string& passphrase) {
  const auto original = checked_ciphertext(location);
  Database database;
  database.open(location.path(), IDYDB_READONLY, passphrase);
  verify_same_file(location, original.get());
  const auto payload = database.extract();
  database.close();
  return payload;
}

void verify_commit(const win::PinnedPath& location, const std::string& payload,
                   const std::string& passphrase) {
  if (read_locked(location, passphrase) != payload)
    throw std::runtime_error("environment database close/reopen verification failed");
}

[[noreturn]] void uncertain() {
  // Publication belongs to IdyDB. A failure may occur after visibility changes.
  // Never assert rollback, retry a close or promote a side file here.
  throw std::runtime_error("environment database commit outcome is uncertain; "
                           "reopen and check the recorded event before retry");
}
}  // namespace

void create_environment_store(const fs::path& database_path, const std::string& payload,
                              const std::string& passphrase) {
  validate_key(passphrase);
  validate_payload(payload);
  const win::PinnedPath location(database_path, true);
  const Lock lock(location, true);
  win::PrivateSecurity security;
  {
    const win::Handle reserved(CreateFileW(win::native_path(location.path()).c_str(),
        GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, security.get(), CREATE_NEW,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!reserved) win::fail("cannot create new Windows environment database");
    win::check_regular(reserved.get(), 0, true);
  }
  // Only our exclusively reserved empty destination is opened with CREATE.
  // The enclosing directory excludes writes by untrusted identities. IdyDB
  // must replace this file atomically and retain equally restrictive permissions.
  try {
    Database database;
    database.open(location.path(), IDYDB_CREATE, passphrase);
    database.insert(payload);
    database.close();
    verify_commit(location, payload, passphrase);
  } catch (...) {
    uncertain();
  }
}

std::string read_environment_store(const fs::path& database_path,
                                   const std::string& passphrase) {
  validate_key(passphrase);
  const win::PinnedPath location(database_path, true);
  // Missing databases never create a lock or an empty replacement.
  const auto present = checked_ciphertext(location);
  const Lock lock(location, false);
  return read_locked(location, passphrase);
}

void replace_environment_store(const fs::path& database_path, const std::string& expected,
                               const std::string& replacement, const std::string& passphrase) {
  validate_key(passphrase);
  validate_payload(expected);
  validate_payload(replacement);
  const win::PinnedPath location(database_path, true);
  const auto present = checked_ciphertext(location);
  const Lock lock(location, true);

  // IdyDB's encrypted writable handle is responsible for locking and
  // authenticated in-memory state. Compare and insert use that SAME handle.
  // Windows atomic publication and crash durability are the supplied library's
  // contract, not a second exchange-owned rename implementation.
  const auto original = checked_ciphertext(location);
  Database database;
  database.open(location.path(), 0, passphrase);
  verify_same_file(location, original.get());
  if (database.extract() != expected)
    throw StoreConflict("environment database changed; reload before recording again");
  try {
    database.insert(replacement);
    database.close();
    verify_commit(location, replacement, passphrase);
  } catch (...) {
    uncertain();
  }
}
}  // namespace ph::exchange::storage
