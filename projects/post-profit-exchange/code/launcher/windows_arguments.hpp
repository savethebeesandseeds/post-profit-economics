// SPDX-License-Identifier: MIT
#pragma once
#include <string>

// Quote one Windows CRT argument. No cmd.exe or PowerShell expression is built.
inline std::wstring quote_windows_argument(const std::wstring& value) {
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(c);
        } else {
            out.append(backslashes, L'\\');
            out.push_back(c);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}
