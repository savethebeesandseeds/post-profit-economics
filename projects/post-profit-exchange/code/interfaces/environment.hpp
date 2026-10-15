// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <stdexcept>
#include <nlohmann/json.hpp>
namespace ph::exchange::environment {
using Json = nlohmann::json;
class Conflict : public std::invalid_argument { public: using std::invalid_argument::invalid_argument; };
Json record_transaction(const std::filesystem::path& root, const std::string& request_text,
                        const std::filesystem::path& key_file = {});
Json initialize(const std::filesystem::path& root, const std::filesystem::path& key_file = {});
Json inspect(const std::filesystem::path& root, const std::filesystem::path& key_file = {});
// Private runtime configuration: never included in account reports.
Json service_configuration(const std::filesystem::path& config);
Json migrate(const std::filesystem::path& source, const std::filesystem::path& destination,
             const std::filesystem::path& key_file);
std::filesystem::path render(const std::filesystem::path& root,
                             const std::filesystem::path& template_path,
                             const std::filesystem::path& key_file = {}, bool allow_plaintext_export = false);
}
