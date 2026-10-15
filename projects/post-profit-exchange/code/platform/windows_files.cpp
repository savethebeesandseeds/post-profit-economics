// SPDX-License-Identifier: MIT
#include "platform/windows_files.hpp"

#include <aclapi.h>
#include <sddl.h>
#include <algorithm>
#include <cwctype>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace ph::exchange::platform::windows {
namespace {
namespace fs = std::filesystem;

struct LocalFreeDeleter {
  void operator()(void* value) const noexcept { if (value) LocalFree(value); }
};
using LocalMemory = std::unique_ptr<void, LocalFreeDeleter>;

std::vector<unsigned char> user_token() {
  HANDLE raw = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
    fail("cannot inspect the current Windows identity");
  const Handle token(raw);
  DWORD size = 0;
  GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
    fail("cannot size the current Windows identity");
  std::vector<unsigned char> result(size);
  if (!GetTokenInformation(token.get(), TokenUser, result.data(), size, &size))
    fail("cannot read the current Windows identity");
  return result;
}

bool trusted_sid(PSID sid, PSID user) {
  return sid && IsValidSid(sid) &&
      (EqualSid(sid, user) || IsWellKnownSid(sid, WinLocalSystemSid) ||
       IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
}

bool reserved_name(std::wstring component) {
  component = component.substr(0, component.find(L'.'));
  while (!component.empty() && component.back() == L' ') component.pop_back();
  std::transform(component.begin(), component.end(), component.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
  if (component == L"CON" || component == L"PRN" || component == L"AUX" ||
      component == L"NUL" || component == L"CLOCK$" ||
      component == L"CONIN$" || component == L"CONOUT$") return true;
  if (component.size() == 4 &&
      (component.substr(0, 3) == L"COM" || component.substr(0, 3) == L"LPT")) {
    const auto digit = component[3];
    return (digit >= L'0' && digit <= L'9') ||
           digit == L'\u00b9' || digit == L'\u00b2' || digit == L'\u00b3';
  }
  return false;
}

void validate_components(const fs::path& path) {
  for (const auto& part : path.relative_path()) {
    const auto name = part.native();
    if (name == L".") continue; // Benign relative workspace spelling; resolved once below.
    if (name.empty() || name == L".." ||
        name.back() == L'.' || name.back() == L' ' || reserved_name(name))
      throw std::runtime_error("Windows path contains an ambiguous or reserved name");
    for (const auto c : name)
      if (c < 32 || c == L':' || c == L'<' || c == L'>' || c == L'"' ||
          c == L'|' || c == L'?' || c == L'*')
        throw std::runtime_error("Windows path contains a stream or invalid character");
  }
}

void check_directory(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info {};
  if (!GetFileInformationByHandle(handle, &info))
    fail("cannot inspect a Windows path directory");
  if (GetFileType(handle) != FILE_TYPE_DISK ||
      !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    throw std::runtime_error("Windows path directories must be local directories without reparse points");
}

Handle open_directory(const fs::path& path) {
  Handle result(CreateFileW(native_path(path).c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!result) fail("cannot pin a Windows path directory");
  check_directory(result.get());
  return result;
}
}  // namespace

[[noreturn]] void fail(const char* message, DWORD code) {
  throw std::system_error(static_cast<int>(code), std::system_category(), message);
}
Handle::~Handle() { reset(); }
Handle::Handle(Handle&& other) noexcept : value_(other.value_) {
  other.value_ = INVALID_HANDLE_VALUE;
}
Handle& Handle::operator=(Handle&& other) noexcept {
  if (this != &other) {
    reset(other.value_);
    other.value_ = INVALID_HANDLE_VALUE;
  }
  return *this;
}
Handle::operator bool() const noexcept {
  return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
}
void Handle::reset(HANDLE value) noexcept {
  if (*this) CloseHandle(value_);
  value_ = value;
}

fs::path local_path(const fs::path& path) {
  const auto input = path.native();
  if (input.empty() || input.find(L'\0') != std::wstring::npos ||
      input.front() == L'\\' || input.front() == L'/' ||
      (path.has_root_name() && !path.has_root_directory()))
    throw std::runtime_error("Windows path must be relative or a local drive path, never UNC or a device path");
  validate_components(path);
  const DWORD needed = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
  if (!needed || needed > 32768) fail("cannot resolve the Windows path");
  std::vector<wchar_t> buffer(needed);
  const DWORD length = GetFullPathNameW(input.c_str(), needed, buffer.data(), nullptr);
  if (!length || length >= needed) fail("cannot resolve the Windows path");
  const fs::path result(std::wstring(buffer.data(), length));
  const auto drive = result.root_name().native();
  if (drive.size() != 2 || drive[1] != L':' ||
      !((drive[0] >= L'A' && drive[0] <= L'Z') ||
        (drive[0] >= L'a' && drive[0] <= L'z')))
    throw std::runtime_error("Windows paths require a local drive letter");
  validate_components(result);
  const auto root = result.root_path().native();
  if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED)
    throw std::runtime_error("Windows exchange files require a fixed local drive");
  wchar_t filesystem[MAX_PATH] {};
  if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
                            filesystem, MAX_PATH))
    fail("cannot inspect the Windows volume");
  if (_wcsicmp(filesystem, L"NTFS") != 0)
    throw std::runtime_error("Windows exchange files currently require NTFS");
  return result;
}

std::wstring native_path(const fs::path& absolute_path) {
  auto value = absolute_path.native();
  std::replace(value.begin(), value.end(), L'/', L'\\');
  return L"\\\\?\\" + value;
}

std::string utf8_path(const fs::path& path) {
  const auto value = path.native();
  if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::runtime_error("Windows path exceeds the UTF-8 conversion bound");
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (!length) fail("cannot encode the Windows path as UTF-8");
  std::string result(static_cast<std::size_t>(length), '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), length, nullptr, nullptr))
    fail("cannot encode the Windows path as UTF-8");
  return result;
}

PinnedPath::PinnedPath(const fs::path& path, bool private_parent) : path_(local_path(path)) {
  if (path_.filename().empty())
    throw std::runtime_error("Windows file path must name a file");
  fs::path cursor = path_.root_path();
  ancestors_.push_back(open_directory(cursor));
  for (const auto& component : path_.parent_path().relative_path()) {
    cursor /= component;
    ancestors_.push_back(open_directory(cursor));
  }
  if (private_parent) require_private_acl(ancestors_.back().get(), true);
}

void require_private_acl(HANDLE handle, bool allow_read_others) {
  const auto token = user_token();
  PSID user = reinterpret_cast<const TOKEN_USER*>(token.data())->User.Sid;
  PSID owner = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const DWORD status = GetSecurityInfo(handle, SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
      &owner, nullptr, &dacl, nullptr, &raw);
  if (status != ERROR_SUCCESS) fail("cannot inspect Windows file permissions", status);
  const LocalMemory descriptor(raw);
  if (!trusted_sid(owner, user) || !dacl || !IsValidAcl(dacl))
    throw std::runtime_error("Windows exchange files require a trusted owner and an explicit access list");

  constexpr ACCESS_MASK mutable_access =
      FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
      FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
  constexpr ACCESS_MASK readable_access = FILE_READ_DATA | FILE_EXECUTE | GENERIC_READ | GENERIC_EXECUTE;
  const ACCESS_MASK restricted = mutable_access | (allow_read_others ? 0 : readable_access);
  for (DWORD i = 0; i < dacl->AceCount; ++i) {
    void* raw_ace = nullptr;
    if (!GetAce(dacl, i, &raw_ace)) fail("cannot inspect a Windows access entry");
    const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
    if (header->AceFlags & INHERIT_ONLY_ACE) continue;
    if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
      throw std::runtime_error("Windows exchange files require ordinary explicit access entries");
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
    PSID sid = const_cast<DWORD*>(&ace->SidStart);
    if ((ace->Mask & restricted) && !trusted_sid(sid, user))
      throw std::runtime_error("Windows exchange file permissions grant access to another identity");
  }
}

void check_regular(HANDLE handle, std::uint64_t max_size, bool private_acl) {
  BY_HANDLE_FILE_INFORMATION info {};
  if (!GetFileInformationByHandle(handle, &info))
    fail("cannot inspect a Windows exchange file");
  if (GetFileType(handle) != FILE_TYPE_DISK ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      info.nNumberOfLinks != 1)
    throw std::runtime_error("Windows exchange files must be regular files without reparse points or hard links");
  const auto size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
  if (size > max_size)
    throw std::runtime_error("Windows exchange file exceeds its size bound");
  if (private_acl) require_private_acl(handle, false);
}

Handle open_regular(const PinnedPath& path, DWORD desired_access, DWORD sharing,
                    std::uint64_t max_size, bool private_acl) {
  Handle result(CreateFileW(native_path(path.path()).c_str(),
      desired_access | FILE_READ_ATTRIBUTES | READ_CONTROL, sharing, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!result) fail("cannot open the Windows exchange file");
  check_regular(result.get(), max_size, private_acl);
  return result;
}

FileIdentity identity(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info {};
  if (!GetFileInformationByHandle(handle, &info))
    fail("cannot inspect the Windows file identity");
  return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}

PrivateSecurity::PrivateSecurity() {
  const auto token = user_token();
  PSID user = reinterpret_cast<const TOKEN_USER*>(token.data())->User.Sid;
  LPWSTR raw_sid = nullptr;
  if (!ConvertSidToStringSidW(user, &raw_sid))
    fail("cannot encode the Windows file owner");
  const LocalMemory sid(raw_sid);
  const std::wstring sddl = std::wstring(L"O:") + raw_sid +
      L"D:P(A;OICI;FA;;;" + raw_sid + L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
      sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
    fail("cannot prepare private Windows file permissions");
  attributes_.nLength = sizeof(attributes_);
  attributes_.lpSecurityDescriptor = descriptor_;
  attributes_.bInheritHandle = FALSE;
}
PrivateSecurity::~PrivateSecurity() { if (descriptor_) LocalFree(descriptor_); }
}  // namespace ph::exchange::platform::windows
