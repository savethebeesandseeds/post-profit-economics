// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace ph::exchange::operations {
// Pure validation and opening balance arithmetic. No accrual, sales or pricing.
nlohmann::json opening_summary(const nlohmann::json& state,
    const std::vector<std::string>& catalog, const std::string& environment_id);
}
