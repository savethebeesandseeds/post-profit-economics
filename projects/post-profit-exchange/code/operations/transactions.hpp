// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ph::exchange::operations {
// Pure accounting for an observed event, with integer minor-unit money and FIFO
// acquisition costs. Returns {"state": opening-shaped state, "effect": deltas}.
// Invalid records throw without changing either input. The application service
// owns transaction deduplication and historical lot-identity checks.
nlohmann::json apply_transaction(const nlohmann::json& current,
    const nlohmann::json& event, const std::vector<std::string>& catalog,
    const std::string& environment_id);
}
