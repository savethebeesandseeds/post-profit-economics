// SPDX-License-Identifier: MIT
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ph::production {
using Json = nlohmann::json;

// Strict parsing and validation throw std::invalid_argument on invalid input.
// simulate() owns local state; it performs no file, network or wall-clock access.
Json parse_json(const std::string& text);
void validate_config(const Json& config);
Json default_config();
Json simulate(const Json& config);
// Versioned command boundary: a validation failure returns an error, not a run.
std::string run_json(const std::string& command);
}  // namespace ph::production
