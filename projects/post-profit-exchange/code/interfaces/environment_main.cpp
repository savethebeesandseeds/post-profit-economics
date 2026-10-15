// SPDX-License-Identifier: MIT
#include "environment.hpp"
#include <cstdio>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/resource.h>
#endif
namespace {
#ifdef _WIN32
int duplicate(int fd) { return ::_dup(fd); }
int redirect(int from,int to) { return ::_dup2(from,to); }
int close_descriptor(int fd) { return ::_close(fd); }
#else
int duplicate(int fd) { return ::dup(fd); }
int redirect(int from,int to) { return ::dup2(from,to); }
int close_descriptor(int fd) { return ::close(fd); }
#endif
// IdyDB's C debug output belongs on stderr; stdout is a single JSON response.
class DiagnosticOutput {
  int saved_;
 public:
  DiagnosticOutput() : saved_(duplicate(1)) {
    std::fflush(stdout);
    if (saved_ < 0) throw std::runtime_error("cannot preserve command output");
    if (redirect(2,1) < 0) {
      close_descriptor(saved_); saved_ = -1;
      throw std::runtime_error("cannot separate database diagnostics from command output");
    }
  }
  ~DiagnosticOutput() { std::fflush(stdout); if (saved_ >= 0) { redirect(saved_,1); close_descriptor(saved_); } }
  DiagnosticOutput(const DiagnosticOutput&) = delete;
  DiagnosticOutput& operator=(const DiagnosticOutput&) = delete;
};
}
int run_command(int argc,char** argv) {
  using namespace ph::exchange;
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "exchange_environment initialize|inspect|transact ENVIRONMENT_DIRECTORY [--key-file PATH]\n"
                 "exchange_environment render ENVIRONMENT_DIRECTORY TEMPLATE_HTML [--key-file PATH --allow-plaintext-export]\n"
                 "exchange_environment migrate SOURCE_DIRECTORY DESTINATION_DIRECTORY --key-file PATH\n"
                 "transact reads JSON from stdin; migrate requires the source writer to be stopped.\n"
                 "Key files contain 64 hexadecimal characters. initialize creates once; inspect never resets.\n";
    return 0;
  }
  try {
#ifdef _WIN32
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#else
    const rlimit no_core{0,0};
    if (::setrlimit(RLIMIT_CORE,&no_core) != 0) throw std::runtime_error("cannot disable core dumps");
#endif
    std::vector<std::string> args;
    std::filesystem::path key_file;
    bool allow_export=false, key_seen=false;
    for (int i=1; i<argc; ++i) {
      const std::string arg=argv[i];
      if (arg == "--key-file") {
        if (key_seen || ++i >= argc || std::string(argv[i]).empty())
          throw std::invalid_argument("missing or repeated --key-file");
        key_seen=true; key_file=std::filesystem::u8path(argv[i]);
      } else if (arg == "--allow-plaintext-export") {
        if (allow_export) throw std::invalid_argument("repeated export option");
        allow_export=true;
      } else if (arg.rfind("--",0) == 0) throw std::invalid_argument("unknown option");
      else args.push_back(arg);
    }
    if (args.empty() || (allow_export && args[0] != "render"))
      throw std::invalid_argument("invalid arguments; use --help");
    environment::Json result;
    {
      DiagnosticOutput diagnostics;
      if (args.size() == 2 && args[0] == "initialize") result=environment::initialize(std::filesystem::u8path(args[1]),key_file);
      else if (args.size() == 2 && args[0] == "inspect") result=environment::inspect(std::filesystem::u8path(args[1]),key_file);
      else if (args.size() == 2 && args[0] == "transact") {
        std::string request;
        char buffer[4096];
        while (std::cin) {
          std::cin.read(buffer,sizeof buffer);
          request.append(buffer,static_cast<std::size_t>(std::cin.gcount()));
          if (request.size() > 16384) throw std::invalid_argument("transaction request exceeds 16 KiB");
        }
        result=environment::record_transaction(std::filesystem::u8path(args[1]),request,key_file);
      }
      else if (args.size() == 3 && args[0] == "render")
        result={{"status","ok"},{"report",environment::render(std::filesystem::u8path(args[1]),std::filesystem::u8path(args[2]),key_file,allow_export).generic_u8string()}};
      else if (args.size() == 3 && args[0] == "migrate")
        result=environment::migrate(std::filesystem::u8path(args[1]),std::filesystem::u8path(args[2]),key_file);
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

#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
  try {
    std::vector<std::string> arguments;
    for(int i=0;i<argc;++i) arguments.push_back(std::filesystem::path(argv[i]).u8string());
    std::vector<char*> pointers;
    for(auto& argument:arguments) pointers.push_back(argument.data());
    return run_command(argc,pointers.data());
  } catch(const std::exception&) {
    std::cerr << "Cannot decode command arguments.\n";
    return 2;
  }
}
#else
int main(int argc,char** argv) { return run_command(argc,argv); }
#endif
