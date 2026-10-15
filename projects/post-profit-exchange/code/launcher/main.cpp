// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "windows_arguments.hpp"

namespace fs = std::filesystem;

static fs::path executable_path() {
    std::vector<wchar_t> value(32768);
    const DWORD length = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    if (!length || length >= value.size()) throw std::runtime_error("Cannot locate this executable.");
    return fs::path(std::wstring(value.data(), length));
}

static fs::path launcher_script() {
    auto current = executable_path().parent_path();
    for (int depth = 0; depth != 8 && !current.empty(); ++depth) {
        const auto script = current / L"projects/post-profit-exchange/code/interfaces/application-container.ps1";
        if (fs::is_regular_file(script)) return script;
        if (current == current.root_path()) break;
        current = current.parent_path();
    }
    throw std::runtime_error("Keep this launcher inside the Post-Profit Economics checkout.");
}

int wmain(int argc, wchar_t** argv) {
    try {
        std::wstring config, action = L"up";
        bool has_action = false;
        for (int i = 1; i < argc; ++i) {
            const std::wstring argument = argv[i];
            if (argument == L"--help") {
                std::wcout << L"Usage: post-profit-exchange.exe --config PATH [--action ACTION]\n"
                    L"ACTION: up (default), plan, provision, status, stop, logs.\n"
                    L"Requires this checkout and Docker Desktop; the server runs inside Debian.\n"
                    L"up/provision opens the browser only after the configured environment is ready.\n";
                return 0;
            }
            if ((argument != L"--config" && argument != L"--action") || i + 1 >= argc)
                throw std::runtime_error("Expected --config PATH and optional --action ACTION. Use --help.");
            if (argument == L"--config") {
                if (!config.empty()) throw std::runtime_error("--config can be given once.");
                config = argv[++i];
                if (config.empty()) throw std::runtime_error("--config cannot be empty.");
            } else {
                if (has_action) throw std::runtime_error("--action can be given once.");
                action = argv[++i];
                has_action = true;
            }
        }
        if (config.empty()) throw std::runtime_error("Supply --config PATH. Use --help for usage.");
        if (action != L"up" && action != L"plan" && action != L"provision" &&
            action != L"status" && action != L"stop" && action != L"logs")
            throw std::runtime_error("Unknown action. Use --help.");
        // Resolve user-relative configuration before PowerShell changes any context.
        config = fs::absolute(config).lexically_normal().wstring();
        const fs::path script = launcher_script();
        std::vector<wchar_t> system(32768);
        const UINT length = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
        if (!length || length >= system.size()) throw std::runtime_error("Cannot locate Windows PowerShell.");
        const auto powershell = fs::path(std::wstring(system.data(), length)) /
            L"WindowsPowerShell/v1.0/powershell.exe";
        const std::vector<std::wstring> arguments = {
            powershell.wstring(), L"-NoLogo", L"-NoProfile", L"-NonInteractive",
            // Only this child process; no user or machine policy is changed.
            L"-ExecutionPolicy", L"Bypass", L"-File", script.wstring(),
            L"-Action", action, L"-Config", config
        };
        std::wstring command;
        for (const auto& argument : arguments) {
            if (!command.empty()) command.push_back(L' ');
            command += quote_windows_argument(argument);
        }
        if (action == L"up" || action == L"provision") command += L" -OpenBrowser";
        if (command.size() >= 32767) throw std::runtime_error("Launch arguments exceed the Windows limit.");
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(powershell.c_str(), buffer.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &startup, &process)) {
            std::cerr << "Cannot start Windows PowerShell (error " << GetLastError() << ").\n";
            return 1;
        }
        CloseHandle(process.hThread);
        const DWORD wait = WaitForSingleObject(process.hProcess, INFINITE);
        DWORD result = 1;
        if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, &result)) {
            CloseHandle(process.hProcess);
            throw std::runtime_error("Cannot read launcher completion status.");
        }
        CloseHandle(process.hProcess);
        return static_cast<int>(result);
    } catch (const std::exception& error) {
        std::cerr << "Post-profit-exchange: " << error.what() << "\n";
        return 1;
    }
}
