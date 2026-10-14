// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <stdexcept>
#include <nlohmann/json.hpp>
namespace ph::exchange::environment {
using Json = nlohmann::json;
class Conflict : public std::invalid_argument { public: using std::invalid_argument::invalid_argument; };
Json record_transaction(const std::filesystem::path& root, const std::string& request_text);
Json initialize(const std::filesystem::path& root);
Json inspect(const std::filesystem::path& root);
std::filesystem::path render(const std::filesystem::path& root,
                             const std::filesystem::path& template_path);
}
