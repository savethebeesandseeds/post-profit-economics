// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace ph::exchange::server_platform {
// Installed before any application thread. Ctrl+C requests an orderly drain.
class Shutdown {
  struct State;
  std::unique_ptr<State> state_;
 public:
  Shutdown();
  ~Shutdown();
  bool requested(std::chrono::milliseconds wait);
  Shutdown(const Shutdown&) = delete;
  Shutdown& operator=(const Shutdown&) = delete;
};
class StartupOutput {
  int output_ = -1;
 public:
  StartupOutput();
  ~StartupOutput();
  void ready(const std::string& utf8_line);
  StartupOutput(const StartupOutput&) = delete;
  StartupOutput& operator=(const StartupOutput&) = delete;
};
std::string session_token();
#ifdef _WIN32
std::filesystem::path executable_path();
std::string utf8(const wchar_t* value);
bool open_browser(const std::string& loopback_url);
#endif
}
