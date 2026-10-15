// SPDX-License-Identifier: MIT
#include "windows_arguments.hpp"
#include <iostream>
#include <utility>
#include <vector>
int main() {
    const std::vector<std::pair<std::wstring, std::wstring>> cases = {
        {L"", L"\"\""},
        {L"plain", L"\"plain\""},
        {L"C:\\a b\\exchange.cfg", L"\"C:\\a b\\exchange.cfg\""},
        {L"C:\\a b\\", L"\"C:\\a b\\\\\""},
        {L"a\"b", L"\"a\\\"b\""},
        {L"a\\\"b", L"\"a\\\\\\\"b\""},
        {L"$(secret);&|", L"\"$(secret);&|\""},
        {L"\u00e9\u4e2d", L"\"\u00e9\u4e2d\""}
    };
    for (const auto& item : cases)
        if (quote_windows_argument(item.first) != item.second) return 1;
    std::cout << "Windows argument quoting: " << cases.size() << " cases passed.\n";
}
