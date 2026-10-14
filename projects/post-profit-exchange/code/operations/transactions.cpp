// SPDX-License-Identifier: MIT
#include "transactions.hpp"
#include "opening.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace ph::exchange::operations {
namespace {
using Json = nlohmann::json;
using Amount = std::int64_t;
constexpr Amount limit = 9000000000000LL;
void require(bool ok, const std::string& message) {
  if (!ok) throw std::invalid_argument(message);
}
void fields(const Json& value, std::initializer_list<const char*> keys) {
  require(value.is_object() && value.size() == keys.size(),
          "transaction has missing or unknown fields");
  for (const auto* key : keys)
    require(value.contains(key), std::string("transaction missing ") + key);
}
Amount number(const Json& value, Amount low = 0, Amount high = limit) {
  require(value.is_number_integer(), "transaction amount or quantity must be an integer");
  if (value.is_number_unsigned())
    require(value.get<std::uint64_t>() <= static_cast<std::uint64_t>(high),
            "transaction integer exceeds limit");
  const auto result = value.get<Amount>();
  require(result >= low && result <= high, "transaction amount or quantity is out of range");
  return result;
}
std::string identifier(const Json& value) {
  require(value.is_string(), "transaction identifier must be text");
  const auto result = value.get<std::string>();
  require(!result.empty() && result.size() <= 64 &&
          std::all_of(result.begin(), result.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
  }), "transaction identifier is invalid");
  return result;
}
Amount add(Amount left, Amount right) {
  require(right >= 0 ? left <= limit - right : left >= -limit - right,
          "transaction total exceeds supported exact amount");
  return left + right;
}
Amount multiply(Amount units, Amount amount) {
  require(amount <= limit / units, "transaction value exceeds supported exact amount");
  return units * amount;
}
Json& product_for(Json& state, const Json& sku) {
  const auto key = identifier(sku);
  for (auto& product : state.at("products"))
    if (product.at("sku") == key) return product;
  throw std::invalid_argument("transaction SKU is not in this operation's catalog");
}
} // namespace

Json apply_transaction(const Json& current, const Json& event,
                       const std::vector<std::string>& catalog,
                       const std::string& environment_id) {
  const auto summary = opening_summary(current, catalog, environment_id);
  fields(event, {"schema_version", "id", "environment_id", "date", "kind", "details"});
  require(event.at("schema_version") == "exchange.transaction.v1",
          "unsupported transaction schema");
  const auto event_id = identifier(event.at("id"));
  require(identifier(event.at("environment_id")) == environment_id,
          "transaction environment identity mismatch");
  require(event.at("kind").is_string(), "transaction kind must be text");
  Json state = current;
  state["as_of"] = event.at("date");
  // Reuse the same calendar validation as persisted state, then compare ISO dates.
  opening_summary(state, catalog, environment_id);
  require(event.at("date").get<std::string>() >= current.at("as_of").get<std::string>(),
          "transaction date precedes the current accounts; backdated entry is not supported");

  const auto kind = event.at("kind").get<std::string>();
  const auto& details = event.at("details");
  const auto cash = number(current.at("cash"));
  const auto reserve = number(current.at("reserve"));
  const auto earned = number(current.at("earned_funding"), -limit);
  Amount cash_delta = 0, inventory_delta = 0, earned_delta = 0;
  Amount revenue = 0, cost_of_goods_sold = 0, expense = 0;

  if (kind == "delivery") {
    fields(details, {"sku", "lot_id", "units", "unit_cost"});
    auto& product = product_for(state, details.at("sku"));
    const auto lot_id = identifier(details.at("lot_id"));
    for (const auto& item : current.at("products"))
      for (const auto& lot : item.at("lots"))
        require(lot.at("id") != lot_id, "delivery lot id already exists");
    const auto units = number(details.at("units"), 1, 1000000);
    const auto unit_cost = number(details.at("unit_cost"));
    inventory_delta = multiply(units, unit_cost);
    require(inventory_delta == 0 ||
            inventory_delta <= summary.at("cash_after_obligations_and_reserve").get<Amount>(),
            "delivery would use cash protected for reserve or outstanding obligations");
    require(product.at("lots").size() < 100, "at most 100 FIFO lots per product supported");
    product["lots"].push_back({{"id", lot_id}, {"units", units}, {"unit_cost", unit_cost}});
    cash_delta = -inventory_delta;
  } else if (kind == "sale") {
    fields(details, {"sku", "units", "unit_price"});
    auto& product = product_for(state, details.at("sku"));
    auto remaining = number(details.at("units"), 1, 1000000);
    const auto unit_price = number(details.at("unit_price"), 1);
    revenue = multiply(remaining, unit_price);
    Json lots = Json::array();
    for (const auto& lot : product.at("lots")) {
      auto units = number(lot.at("units"), 1, 1000000);
      const auto consumed = std::min(remaining, units);
      if (consumed != 0)
        cost_of_goods_sold = add(cost_of_goods_sold,
            multiply(consumed, number(lot.at("unit_cost"))));
      remaining -= consumed;
      units -= consumed;
      if (units != 0) {
        Json retained = lot;
        retained["units"] = units;
        lots.push_back(std::move(retained));
      }
    }
    require(remaining == 0, "sale exceeds available stock");
    product["lots"] = std::move(lots);
    cash_delta = revenue;
    inventory_delta = -cost_of_goods_sold;
    earned_delta = revenue - cost_of_goods_sold;
  } else if (kind == "expense") {
    fields(details, {"category", "description", "amount", "settlement"});
    const auto& category = details.at("category");
    require(category == "wages" || category == "operating", "unsupported expense category");
    require(details.at("description").is_string(), "expense description must be text");
    const auto description = details.at("description").get<std::string>();
    require(!description.empty() && description.size() <= 512 &&
            description.find('\0') == std::string::npos,
            "expense description must contain 1 to 512 bytes of text without NUL");
    expense = number(details.at("amount"), 1);
    earned_delta = -expense;
    const auto& settlement = details.at("settlement");
    if (settlement == "paid") {
      // This is a newly incurred expense, not payment of an existing obligation.
      // Protect reserve; any pre-existing obligation remains outstanding.
      require(expense <= cash - reserve, "paid expense would use protected reserve or exceed cash");
      cash_delta = -expense;
    } else if (settlement == "unpaid") {
      auto& debts = state.at("obligations");
      require(debts.size() < 100, "at most 100 outstanding obligations supported");
      for (const auto& debt : debts)
        require(debt.at("id") != event_id, "expense obligation id already exists");
      debts.push_back({{"id", event_id}, {"kind", category}, {"amount", expense}});
    } else {
      throw std::invalid_argument("expense settlement must be paid or unpaid");
    }
  } else {
    throw std::invalid_argument("unsupported transaction kind");
  }

  state["cash"] = add(cash, cash_delta);
  state["earned_funding"] = add(earned, earned_delta);
  opening_summary(state, catalog, environment_id);
  return {{"state", std::move(state)},
          {"effect", {{"cash_delta", cash_delta}, {"inventory_value_delta", inventory_delta},
                      {"earned_funding_delta", earned_delta}, {"revenue", revenue},
                      {"cost_of_goods_sold", cost_of_goods_sold}, {"expense", expense}}}};
}
} // namespace ph::exchange::operations
