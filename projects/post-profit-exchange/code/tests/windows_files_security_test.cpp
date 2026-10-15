// SPDX-License-Identifier: MIT
// Real Win32 filesystem/ACL/locking checks. No IdyDB substitute is linked.
// Wine execution is a compatibility check, not evidence of NTFS crash durability.
#include "platform/windows_files.hpp"
#include "storage/windows_lock.hpp"
#include <aclapi.h>
#include <sddl.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
namespace win = ph::exchange::platform::windows;
using StableLock = ph::exchange::storage::windows::StableLock;
namespace {
int checks = 0, skipped = 0;
bool wine_runtime() { return GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr; }
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
    win::fail("cannot create the isolated security fixture directory");
}
void file(const fs::path& path, const std::string& bytes) {
  win::PrivateSecurity security;
  win::Handle handle(CreateFileW(win::native_path(path).c_str(), GENERIC_WRITE,
      0, security.get(), CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!handle) win::fail("cannot create the isolated security fixture file");
  DWORD written = 0;
  if (!WriteFile(handle.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
      written != bytes.size()) win::fail("cannot write the isolated security fixture");
}
void permissions(const fs::path& path, const wchar_t* other_access) {
  win::PrivateSecurity private_security;
  auto original = private_security.get()->lpSecurityDescriptor;
  PSID owner = nullptr; BOOL defaulted = FALSE;
  if (!GetSecurityDescriptorOwner(original, &owner, &defaulted))
    win::fail("cannot inspect isolated test owner");
  LPWSTR sid = nullptr;
  if (!ConvertSidToStringSidW(owner, &sid)) win::fail("cannot encode isolated test owner");
  const std::wstring user(sid); LocalFree(sid);
  const std::wstring text = L"D:P(A;;FA;;;" + user + L")(A;;FA;;;SY)(A;;FA;;;BA)" + other_access;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1,
                                                             &descriptor, nullptr))
    win::fail("cannot prepare isolated test permissions");
  const BOOL result = SetFileSecurityW(win::native_path(path).c_str(),
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
  const DWORD error = GetLastError();
  LocalFree(descriptor);
  if (!result) win::fail("cannot set isolated test permissions", error);
}
}  // namespace

int main() {
  fs::path root;
  try {
    wchar_t temporary[32768];
    const DWORD length = GetTempPathW(32768, temporary);
    check(length && length < 32768, "temporary directory is available");
    root = win::local_path(fs::path(temporary) /
        (L"exchange-security-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64())));
    directory(root);
    const auto data = root / L"data";
    directory(data);
    const auto target = data / L"record.idydb";
    file(target, "IDYDBENC-test-only-ciphertext-marker");
    const auto unicode = data / L"cl\u00e9.key";
    file(unicode, std::string(64, 'a'));
    const auto escaped = root / L"rename-attempt";

    for (const auto* path : {L"\\\\server\\share\\file", L"\\\\?\\C:\\file",
         L"\\\\.\\PhysicalDrive0", L"C:relative", L"C:\\tmp\\record:stream",
         L"C:\\tmp\\CON", L"C:\\tmp\\nul.json", L"C:\\tmp\\com1",
         L"C:\\tmp\\lpt\u00b2.txt", L"C:\\tmp\\name.", L"C:\\tmp\\name ",
         L"C:\\tmp\\..\\file", L"C:\\tmp\\invalid?"})
      rejects([&] { win::local_path(path); }, "unsafe Windows path syntax is rejected");
    bool moved_directory = false;
    {
      const win::PinnedPath pinned(unicode, true);
      const auto handle = win::open_regular(pinned, GENERIC_READ, FILE_SHARE_READ, 64, true);
      check(bool(handle), "Unicode private key fixture opens by handle");
      rejects([&] { win::open_regular(pinned, GENERIC_READ, FILE_SHARE_READ, 63, true); },
              "file bound is enforced before reading");
      moved_directory = MoveFileExW(win::native_path(data).c_str(), win::native_path(escaped).c_str(), 0);
      if (moved_directory && wine_runtime()) {
        ++skipped;
        std::cout << "SKIP: Wine does not enforce directory DELETE sharing. Native NTFS required.\n";
      } else check(!moved_directory, "pinned ancestor cannot be renamed");
    }

    if (moved_directory)
      check(MoveFileExW(win::native_path(escaped).c_str(), win::native_path(data).c_str(), 0),
            "restore only the isolated Wine fixture directory");
    const auto alias = data / L"alias.key";
    check(CreateHardLinkW(win::native_path(alias).c_str(), win::native_path(unicode).c_str(), nullptr),
          "create isolated hard-link fixture");
    rejects([&] { const win::PinnedPath path(unicode); win::open_regular(path); },
            "original with an extra hard link is rejected");
    rejects([&] { const win::PinnedPath path(alias); win::open_regular(path); },
            "hard-link alias is rejected");
    check(DeleteFileW(win::native_path(alias).c_str()), "remove only isolated hard-link fixture");

    permissions(unicode, L"(A;;FR;;;WD)");
    {
      const win::PinnedPath pinned(unicode, true);
      rejects([&] { win::open_regular(pinned, GENERIC_READ, FILE_SHARE_READ, 64, true); },
              "key readable by Everyone is rejected");
      const auto handle = win::open_regular(pinned);
      win::require_private_acl(handle.get(), true);
      check(true, "ciphertext may be readable but not writable by other identities");
    }
    permissions(unicode, L"(A;;FA;;;WD)");
    {
      const win::PinnedPath pinned(unicode, true);
      const auto handle = win::open_regular(pinned);
      rejects([&] { win::require_private_acl(handle.get(), true); },
              "file writable by Everyone is rejected");
    }
    permissions(unicode, L"");
    {
      win::Handle handle(CreateFileW(win::native_path(unicode).c_str(), READ_CONTROL | WRITE_DAC,
          FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
      check(bool(handle), "open isolated file to test null DACL");
      check(SetSecurityInfo(handle.get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                            nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS,
            "set isolated null DACL");
      rejects([&] { win::require_private_acl(handle.get()); }, "null DACL is rejected");
    }
    permissions(unicode, L"");
    permissions(data, L"(A;;FA;;;WD)");
    rejects([&] { const win::PinnedPath pinned(target, true); },
            "data directory writable by Everyone is rejected");
    permissions(data, L"");

    const auto link = data / L"symlink.key";
    if (CreateSymbolicLinkW(win::native_path(link).c_str(),
                            win::native_path(unicode).c_str(), 0x2)) {
      const auto attributes = GetFileAttributesW(win::native_path(link).c_str());
      if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        rejects([&] { const win::PinnedPath pinned(link); win::open_regular(pinned); },
                "file reparse point is rejected");
        check(DeleteFileW(win::native_path(link).c_str()), "remove isolated reparse fixture");
      } else {
        // Wine versions may return success without creating a reparse point.
        ++skipped;
        std::cout << "SKIP: runtime did not create a real Windows reparse fixture.\n";
      }
    } else if (GetLastError() == ERROR_PRIVILEGE_NOT_HELD ||
               GetLastError() == ERROR_NOT_SUPPORTED || GetLastError() == ERROR_INVALID_PARAMETER) {
      ++skipped;
      std::cout << "SKIP: this runtime cannot create the reparse fixture.\n";
    } else win::fail("cannot create isolated reparse fixture");

    const win::PinnedPath pinned(target, true);
    std::atomic<bool> started {false}, acquired {false};
    std::exception_ptr failure;
    std::thread contender;
    bool blocked = false, deletion_denied = false;
    {
      const StableLock held(pinned, true);
      contender = std::thread([&] {
        try {
          started = true;
          const StableLock waiting(pinned, true);
          acquired = true;
        } catch (...) { failure = std::current_exception(); }
      });
      while (!started) std::this_thread::yield();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      blocked = !acquired;
      const auto lock_path = fs::path(target.native() + L".lock");
      deletion_denied = !DeleteFileW(win::native_path(lock_path).c_str());
    }
    contender.join();
    if (failure) std::rethrow_exception(failure);
    check(blocked, "exclusive writer lock blocks a competing writer");
    check(deletion_denied, "held stable lock cannot be deleted");
    check(acquired, "waiting writer proceeds after release");
    {
      const StableLock first(pinned, false);
      const StableLock second(pinned, false);
      check(true, "readers can share the stable lock");
    }
    // Only synthetic files beneath the unique fixture directory are cleaned up.
    // PinnedPath keeps data alive until scope exit; cleanup occurs below.
    std::cout << "Windows filesystem security: " << checks << " checks passed; "
              << skipped << " runtime-dependent cases skipped.\n";
  } catch (const std::exception& error) {
    std::cerr << "Windows filesystem security: " << error.what()
              << "\nIsolated fixture retained for inspection.\n";
    return 1;
  }
  fs::remove_all(root);
  return skipped ? 77 : 0;
}
