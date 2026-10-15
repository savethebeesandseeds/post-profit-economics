// SPDX-License-Identifier: MIT
#include "server_platform.hpp"
#include <array>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace ph::exchange::server_platform {
#ifdef _WIN32
namespace {
// These two handles remain valid until process exit. Closing a completion
// handle while Windows is running a console callback would race its wait.
HANDLE shutdown_requested = nullptr;
HANDLE shutdown_completed = nullptr;
BOOL WINAPI console_control(DWORD kind) {
  switch (kind) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
      return SetEvent(shutdown_requested);
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      SetEvent(shutdown_requested);
      // Windows may impose its own termination deadline. Never force-terminate
      // a pending commit ourselves or describe a forced close as a rollback.
      WaitForSingleObject(shutdown_completed, 4000);
      return TRUE;
    default:
      return FALSE;
  }
}
}
struct Shutdown::State {};
Shutdown::Shutdown() : state_(std::make_unique<State>()) {
  if (shutdown_requested || shutdown_completed)
    throw std::runtime_error("shutdown handler already configured");
  shutdown_requested = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  shutdown_completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!shutdown_requested || !shutdown_completed ||
      !SetConsoleCtrlHandler(console_control, TRUE))
    throw std::runtime_error("cannot configure application shutdown");
  // Do not show OS fault/open-file dialogs containing operational paths.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}
Shutdown::~Shutdown() {
  SetEvent(shutdown_completed);
  SetConsoleCtrlHandler(console_control, FALSE);
}
bool Shutdown::requested(std::chrono::milliseconds wait) {
  const auto result = WaitForSingleObject(shutdown_requested, static_cast<DWORD>(wait.count()));
  if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT)
    throw std::runtime_error("cannot wait for application shutdown");
  return result == WAIT_OBJECT_0;
}
#else
struct Shutdown::State { sigset_t signals; };
Shutdown::Shutdown() : state_(std::make_unique<State>()) {
  ::sigemptyset(&state_->signals);
  ::sigaddset(&state_->signals, SIGTERM);
  ::sigaddset(&state_->signals, SIGINT);
  if (::pthread_sigmask(SIG_BLOCK, &state_->signals, nullptr) != 0)
    throw std::runtime_error("cannot configure service shutdown");
  ::signal(SIGPIPE, SIG_IGN);
  const rlimit no_core{0,0};
  if (::setrlimit(RLIMIT_CORE, &no_core) != 0)
    throw std::runtime_error("cannot disable core dumps");
}
Shutdown::~Shutdown() = default;
bool Shutdown::requested(std::chrono::milliseconds wait) {
  const timespec interval{static_cast<time_t>(wait.count()/1000),
                          static_cast<long>((wait.count()%1000)*1000000)};
  const int signal = ::sigtimedwait(&state_->signals, nullptr, &interval);
  if (signal < 0 && errno != EAGAIN && errno != EINTR)
    throw std::runtime_error("cannot wait for service shutdown");
  return signal == SIGTERM || signal == SIGINT;
}
#endif

std::string session_token() {
  std::array<unsigned char,32> bytes{};
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("cannot obtain session randomness");
#else
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::getrandom(bytes.data()+offset, bytes.size()-offset, 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) throw std::runtime_error("cannot obtain session randomness");
    offset += static_cast<std::size_t>(count);
  }
#endif
  const char* digits = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size()*2);
  for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
  return result;
}

// Keep a private readiness descriptor while database libraries receive a null
// stdout stream. No database diagnostic or account record is forwarded.
StartupOutput::StartupOutput() {
  std::cout.flush();
#ifdef _WIN32
  output_ = ::_dup(::_fileno(stdout));
  const int sink = ::_open("NUL", _O_WRONLY | _O_BINARY | _O_NOINHERIT);
  if (output_ < 0 || sink < 0 || ::_dup2(sink, ::_fileno(stdout)) != 0) {
    if (sink >= 0) ::_close(sink);
    if (output_ >= 0) ::_close(output_);
    throw std::runtime_error("cannot configure application output");
  }
  ::_close(sink);
  if (!SetStdHandle(STD_OUTPUT_HANDLE,
                    reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(stdout))))) {
    ::_close(output_);
    throw std::runtime_error("cannot configure Windows application output");
  }
#else
  output_ = ::dup(STDOUT_FILENO);
  const int sink = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
  if (output_ < 0 || sink < 0 || ::dup2(sink,STDOUT_FILENO) < 0) {
    if (sink >= 0) ::close(sink);
    if (output_ >= 0) ::close(output_);
    throw std::runtime_error("cannot configure service output");
  }
  ::close(sink);
#endif
}
StartupOutput::~StartupOutput() {
#ifdef _WIN32
  if (output_ >= 0) ::_close(output_);
#else
  if (output_ >= 0) ::close(output_);
#endif
}
void StartupOutput::ready(const std::string& utf8_line) {
  const auto text = utf8_line + "\n";
  std::size_t offset = 0;
  while (offset < text.size()) {
#ifdef _WIN32
    const auto count = ::_write(output_, text.data()+offset,
                                static_cast<unsigned int>(text.size()-offset));
#else
    const auto count = ::write(output_, text.data()+offset, text.size()-offset);
#endif
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) throw std::runtime_error("cannot report application readiness");
    offset += static_cast<std::size_t>(count);
  }
}

#ifdef _WIN32
std::filesystem::path executable_path() {
  std::vector<wchar_t> path(32768);
  const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!length || length >= path.size()) throw std::runtime_error("cannot locate application");
  return std::filesystem::path(std::wstring(path.data(), length));
}
std::string utf8(const wchar_t* value) {
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                      nullptr, 0, nullptr, nullptr);
  if (size <= 0) throw std::invalid_argument("invalid Unicode command argument");
  std::string result(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                          result.data(), size, nullptr, nullptr) != size)
    throw std::invalid_argument("invalid Unicode command argument");
  result.pop_back();
  return result;
}
bool open_browser(const std::string& loopback_url) {
  // Only our constructed address reaches the Windows shell: never a config
  // string, path or user-provided browser command.
  constexpr char prefix[] = "http://127.0.0.1:";
  if (loopback_url.compare(0, sizeof(prefix)-1, prefix) != 0) return false;
  const auto port = loopback_url.substr(sizeof(prefix)-1);
  if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
    return false;
  const std::wstring address(loopback_url.begin(), loopback_url.end());
  SHELLEXECUTEINFOW request{};
  request.cbSize = sizeof(request);
  request.fMask = SEE_MASK_FLAG_NO_UI;
  request.lpVerb = L"open";
  request.lpFile = address.c_str();
  request.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&request) != FALSE;
}
#endif
}
