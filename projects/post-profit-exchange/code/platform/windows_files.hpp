// SPDX-License-Identifier: MIT
#pragma once
#ifndef _WIN32
#error Windows file helpers require a Windows target.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ph::exchange::platform::windows {
class Handle {
 public:
  explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
  ~Handle();
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept;
  Handle& operator=(Handle&& other) noexcept;
  HANDLE get() const noexcept { return value_; }
  explicit operator bool() const noexcept;
  void reset(HANDLE value = INVALID_HANDLE_VALUE) noexcept;
 private:
  HANDLE value_;
};

// Relative inputs are resolved once. UNC, device namespaces, alternate streams,
// drive-relative names, reserved names and ambiguous trailing characters fail.
// This first Windows release is restricted to fixed local NTFS volumes.
std::filesystem::path local_path(const std::filesystem::path& path);
std::wstring native_path(const std::filesystem::path& absolute_path);
std::string utf8_path(const std::filesystem::path& path);

class PinnedPath {
 public:
  explicit PinnedPath(const std::filesystem::path& path, bool private_parent = false);
  const std::filesystem::path& path() const noexcept { return path_; }
 private:
  std::filesystem::path path_;
  std::vector<Handle> ancestors_;
};

// Trust is limited to the current user, LocalSystem and local Administrators.
// Administrators/same-user malicious processes are outside this boundary.
// Other principals can have read permissions only when explicitly allowed.
void require_private_acl(HANDLE handle, bool allow_read_others = false);
void check_regular(HANDLE handle, std::uint64_t max_size, bool private_acl = false);
Handle open_regular(const PinnedPath& path, DWORD desired_access = GENERIC_READ,
                    DWORD sharing = FILE_SHARE_READ, std::uint64_t max_size = 131072,
                    bool private_acl = false);

struct FileIdentity {
  DWORD volume;
  DWORD high;
  DWORD low;
  bool operator==(const FileIdentity& other) const noexcept {
    return volume == other.volume && high == other.high && low == other.low;
  }
  bool operator!=(const FileIdentity& other) const noexcept { return !(*this == other); }
};
FileIdentity identity(HANDLE handle);

// Use only when creating a new object. Never changes an existing object's ACL.
class PrivateSecurity {
 public:
  PrivateSecurity();
  ~PrivateSecurity();
  PrivateSecurity(const PrivateSecurity&) = delete;
  PrivateSecurity& operator=(const PrivateSecurity&) = delete;
  SECURITY_ATTRIBUTES* get() noexcept { return &attributes_; }
 private:
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
  SECURITY_ATTRIBUTES attributes_ {};
};
[[noreturn]] void fail(const char* message, DWORD code = GetLastError());
}  // namespace ph::exchange::platform::windows
