// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
namespace ph::exchange::platform {
std::string read_regular_file(const std::filesystem::path& path, std::size_t limit);
std::string read_key_file(const std::filesystem::path& path);
void validate_existing_path(const std::filesystem::path& path);
// Windows host configuration resolution; secret bytes never enter configuration.
std::filesystem::path key_file_for_config(const std::filesystem::path& config,
    const std::string& configured_workspace, const std::string& configured_key);
void write_report(const std::filesystem::path& temporary,
    const std::filesystem::path& destination, const std::string& page);
}
