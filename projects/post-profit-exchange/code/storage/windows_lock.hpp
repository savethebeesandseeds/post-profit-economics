// SPDX-License-Identifier: MIT
#pragma once
#include "platform/windows_files.hpp"

namespace ph::exchange::storage::windows {
class StableLock {
 public:
  StableLock(const platform::windows::PinnedPath& database, bool exclusive) {
    const platform::windows::PinnedPath path(std::filesystem::path(database.path().native() + L".lock"), true);
    platform::windows::PrivateSecurity security;
    // This sibling is never removed, truncated or renamed. Deny DELETE sharing
    // for its entire lifetime so independent processes coordinate on one file.
    // Only creating a NEW lock applies the private ACL; existing ACLs are checked.
    handle_ = platform::windows::Handle(CreateFileW(platform::windows::native_path(path.path()).c_str(),
        GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        security.get(), OPEN_ALWAYS,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle_) platform::windows::fail("cannot open the Windows environment writer lock");
    platform::windows::check_regular(handle_.get(), 128 * 1024);
    platform::windows::require_private_acl(handle_.get(), true);
    if (!LockFileEx(handle_.get(), exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0,
                    0, 1, 0, &position_))
      platform::windows::fail("cannot acquire the Windows environment lock");
    locked_ = true;
  }
  ~StableLock() {
    if (locked_) UnlockFileEx(handle_.get(), 0, 1, 0, &position_);
  }
  StableLock(const StableLock&) = delete;
  StableLock& operator=(const StableLock&) = delete;
 private:
  platform::windows::Handle handle_;
  OVERLAPPED position_ {};
  bool locked_ = false;
};

}  // namespace ph::exchange::storage::windows
