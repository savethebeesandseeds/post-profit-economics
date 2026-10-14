// SPDX-License-Identifier: MIT
#include "environment.hpp"
#include <cstdio>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unistd.h>
namespace {
// IdyDB's C debug output belongs on stderr; stdout is a single JSON response.
class DiagnosticOutput {
  int saved_;
 public:
  DiagnosticOutput() : saved_(::dup(STDOUT_FILENO)) {
    std::fflush(stdout);
    if (saved_ < 0) throw std::runtime_error("cannot preserve command output");
    if (::dup2(STDERR_FILENO,STDOUT_FILENO) < 0) {
      ::close(saved_); saved_ = -1;
      throw std::runtime_error("cannot separate database diagnostics from command output");
    }
  }
  ~DiagnosticOutput() { std::fflush(stdout); if (saved_ >= 0) { ::dup2(saved_,STDOUT_FILENO); ::close(saved_); } }
  DiagnosticOutput(const DiagnosticOutput&) = delete;
  DiagnosticOutput& operator=(const DiagnosticOutput&) = delete;
};
}
int main(int argc,char** argv) {
  using namespace ph::exchange;
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "exchange_environment initialize ENVIRONMENT_DIRECTORY\n"
                 "exchange_environment inspect ENVIRONMENT_DIRECTORY\n"
                 "exchange_environment transact ENVIRONMENT_DIRECTORY < request.json\n"
                 "exchange_environment render ENVIRONMENT_DIRECTORY TEMPLATE_HTML\n"
                 "initialize creates once; inspect never resets; render refreshes reports/environment.html.\n";
    return 0;
  }
  try {
    environment::Json result;
    {
      DiagnosticOutput diagnostics;
      if (argc == 3 && std::string(argv[1]) == "initialize") result=environment::initialize(argv[2]);
      else if (argc == 3 && std::string(argv[1]) == "inspect") result=environment::inspect(argv[2]);
      else if (argc == 3 && std::string(argv[1]) == "transact") {
        std::string request;
        char buffer[4096];
        while (std::cin) {
          std::cin.read(buffer,sizeof buffer);
          request.append(buffer,static_cast<std::size_t>(std::cin.gcount()));
          if (request.size() > 16384) throw std::invalid_argument("transaction request exceeds 16 KiB");
        }
        result=environment::record_transaction(argv[2],request);
      }
      else if (argc == 4 && std::string(argv[1]) == "render")
        result={{"status","ok"},{"report",environment::render(argv[2],argv[3]).generic_string()}};
      else throw std::invalid_argument("invalid arguments; use --help");
    }
    std::cout << result.dump(2) << '\n';
    return 0;
  } catch (const environment::Conflict& error) {
    std::cout << environment::Json({{"status","error"},{"error_type","conflict"},{"commit_status","not_recorded"},{"message",error.what()}}).dump() << '\n';
    return 2;
  } catch (const std::invalid_argument& error) {
    std::cout << environment::Json({{"status","error"},{"error_type","validation"},{"message",error.what()}}).dump() << '\n';
    return 2;
  } catch (const nlohmann::json::exception& error) {
    std::cout << environment::Json({{"status","error"},{"error_type","validation"},{"message",error.what()}}).dump() << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cout << environment::Json({{"status","error"},{"error_type","storage"},{"message",error.what()}}).dump() << '\n';
    return 2;
  }
}
