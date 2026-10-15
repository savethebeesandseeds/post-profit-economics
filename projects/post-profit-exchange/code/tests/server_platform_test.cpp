// SPDX-License-Identifier: MIT
#include "../interfaces/server_platform.hpp"
#include <chrono>
#include <iostream>
#include <set>
#include <charconv>
#include <cstdint>
#include <limits>
#include <vector>
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace platform = ph::exchange::server_platform;
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
#ifdef _WIN32
int stop_server(const std::string& supplied_pid) {
  std::uint64_t value=0;
  const auto parsed=std::from_chars(supplied_pid.data(), supplied_pid.data()+supplied_pid.size(),value);
  check(parsed.ec == std::errc() && parsed.ptr == supplied_pid.data()+supplied_pid.size() &&
          value > 0 && value <= std::numeric_limits<DWORD>::max(), "invalid test server PID");
  const DWORD pid=static_cast<DWORD>(value);
  const HANDLE process=OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
  check(process != nullptr, "cannot open test server process");
  struct Close { HANDLE handle; ~Close() { CloseHandle(handle); } } close{process};
  std::vector<wchar_t> executable(32768);
  DWORD length=static_cast<DWORD>(executable.size());
  check(QueryFullProcessImageNameW(process,0,executable.data(),&length) != FALSE,
        "cannot verify test server identity");
  const auto basename=std::filesystem::path(std::wstring(executable.data(),length)).filename().wstring();
  check(_wcsicmp(basename.c_str(),L"post-profit-exchange.exe") == 0,
        "target is not the native exchange test executable");
  FreeConsole();
  check(AttachConsole(pid) != FALSE, "test server must have its own console");
  // This helper shares only the isolated server console, and must survive its
  // own event long enough to verify shutdown. It never terminates the server.
  check(SetConsoleCtrlHandler(nullptr,TRUE) != FALSE, "cannot protect test controller");
  check(GenerateConsoleCtrlEvent(CTRL_C_EVENT,0) != FALSE, "cannot request test server shutdown");
  const DWORD wait=WaitForSingleObject(process,30000);
  FreeConsole();
  check(wait == WAIT_OBJECT_0, "server did not stop within the test deadline");
  DWORD exit_code=1;
  check(GetExitCodeProcess(process,&exit_code) != FALSE && exit_code == 0,
        "server did not exit cleanly");
  return 0;
}
#endif
int main(int argc, char** argv) {
  try {
#ifdef _WIN32
    if (argc == 3 && std::string(argv[1]) == "--stop-server")
      return stop_server(argv[2]);
#endif
    // Keep shutdown signal testing opt-in: the ordinary test never sends an
    // event to a caller's console. The explicit child owns an isolated console.
    const bool signals = argc == 2 && std::string(argv[1]) == "--shutdown";
    const bool output = argc == 2 && std::string(argv[1]) == "--output";
    check(argc == 1 || signals || output, "unexpected test argument");
    if (output) {
      platform::StartupOutput redirect;
      std::cout << "private diagnostic must not escape" << std::flush;
#ifdef _WIN32
      const char diagnostic[] = "Windows handle diagnostic must not escape";
      DWORD written=0;
      check(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), diagnostic,
                      sizeof(diagnostic)-1, &written, nullptr) != FALSE,
            "cannot write redirected Windows diagnostic");
#endif
      redirect.ready("{\"ready\":true}");
      return 0;
    }
    if (signals) {
#ifdef _WIN32
      FreeConsole();
      check(AllocConsole() != FALSE, "cannot allocate isolated test console");
#endif
      platform::Shutdown shutdown;
      check(!shutdown.requested(std::chrono::milliseconds(0)), "shutdown starts requested");
#ifdef _WIN32
      check(GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) != FALSE, "cannot generate test control event");
#else
      check(::kill(::getpid(), SIGTERM) == 0, "cannot generate test signal");
#endif
      check(shutdown.requested(std::chrono::seconds(3)), "shutdown event was not received");
      return 0;
    }
    std::set<std::string> tokens;
    for (int i=0; i<128; ++i) {
      const auto token=platform::session_token();
      check(token.size() == 64, "incorrect session token length");
      check(token.find_first_not_of("0123456789abcdef") == std::string::npos, "invalid token encoding");
      check(tokens.insert(token).second, "repeated session token");
    }
#ifdef _WIN32
    check(platform::utf8(L"C:\\example\\\u00e9change\\\u4ea4\u6613.cfg") ==
            u8"C:\\example\\\u00e9change\\\u4ea4\u6613.cfg", "Unicode argument conversion failed");
    check(!platform::executable_path().empty(), "executable path is empty");
    check(!platform::open_browser("https://example.com"), "external browser URL accepted");
    check(!platform::open_browser("http://127.0.0.1:8766/unsafe"), "browser path accepted");
    check(!platform::open_browser("http://127.0.0.1:8766 & command"), "browser argument injection accepted");
    check(!platform::open_browser("http://127.0.0.1:"), "empty browser port accepted");
#endif
    std::cout << "Server platform checks passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
