// SPDX-License-Identifier: MIT
#include "opening.hpp"
#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
namespace ph::exchange::operations {
namespace {
using Json = nlohmann::json;
using Amount = std::int64_t;
constexpr Amount limit = 9000000000000LL;
void require(bool ok, const std::string& message) {
  if (!ok) throw std::invalid_argument(message);
}
void fields(const Json& v, std::initializer_list<const char*> keys) {
  require(v.is_object() && v.size() == keys.size(), "opening record has missing or unknown fields");
  for (const auto* k : keys) require(v.contains(k), std::string("opening record missing ") + k);
}
Amount number(const Json& v, Amount low = 0, Amount high = limit) {
  require(v.is_number_integer(), "opening amount or quantity must be an integer");
  if (v.is_number_unsigned()) require(v.get<std::uint64_t>() <= static_cast<std::uint64_t>(high), "opening integer exceeds limit");
  const auto n = v.get<Amount>();
  require(n >= low && n <= high, "opening amount or quantity is out of range");
  return n;
}
std::string id(const Json& v) {
  require(v.is_string(), "record identifier must be text");
  const auto s = v.get<std::string>();
  require(!s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
  }), "record identifier is invalid");
  return s;
}
Amount add(Amount a, Amount b) {
  require(a <= limit - b, "opening total exceeds supported exact amount");
  return a + b;
}
void date(const Json& value) {
  require(value.is_string(), "as_of must be a YYYY-MM-DD date");
  const auto s = value.get<std::string>();
  require(s.size() == 10 && s[4] == '-' && s[7] == '-', "as_of must be a YYYY-MM-DD date");
  for (std::size_t i=0; i<s.size(); ++i)
    if (i != 4 && i != 7) require(s[i] >= '0' && s[i] <= '9', "as_of date is invalid");
  int y=std::stoi(s.substr(0,4)), m=std::stoi(s.substr(5,2)), d=std::stoi(s.substr(8,2));
  require(y >= 1 && m >= 1 && m <= 12, "as_of date is invalid");
  const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = y%4 == 0 && (y%100 != 0 || y%400 == 0);
  require(d >= 1 && d <= days[m-1] + (m == 2 && leap ? 1 : 0), "as_of date is invalid");
}
}
Json opening_summary(const Json& state, const std::vector<std::string>& catalog,
                     const std::string& environment_id) {
  fields(state, {"schema_version","environment_id","as_of","cash","reserve",
                 "earned_funding","obligations","products"});
  require(state.at("schema_version") == "exchange.opening.v1", "unsupported opening schema");
  require(id(state.at("environment_id")) == environment_id, "opening environment identity mismatch");
  date(state.at("as_of"));
  const auto cash = number(state.at("cash")), reserve = number(state.at("reserve"));
  require(reserve <= cash, "reserve cannot exceed cash");
  number(state.at("earned_funding"), -limit);
  const auto& debts=state.at("obligations");
  require(debts.is_array() && debts.size() <= 100, "at most 100 opening obligations supported");
  Amount obligations=0, inventory=0, units_total=0;
  std::set<std::string> obligation_ids, product_ids, lot_ids;
  for (const auto& debt : debts) {
    fields(debt, {"id","kind","amount"});
    require(obligation_ids.insert(id(debt.at("id"))).second, "duplicate obligation id");
    require(debt.at("kind") == "wages" || debt.at("kind") == "operating", "unsupported obligation kind");
    obligations=add(obligations,number(debt.at("amount"),1));
  }
  const auto& products=state.at("products");
  require(products.is_array() && !products.empty() && products.size() <= 12,
          "opening requires 1 to 12 products");
  for (const auto& product : products) {
    fields(product, {"sku","published_price","replacement_cost","lots"});
    require(product_ids.insert(id(product.at("sku"))).second, "duplicate opening SKU");
    number(product.at("published_price"),1);
    number(product.at("replacement_cost"),1);
    const auto& lots=product.at("lots");
    require(lots.is_array() && lots.size() <= 100, "at most 100 FIFO lots per product supported");
    for (const auto& lot : lots) {
      fields(lot, {"id","units","unit_cost"});
      require(lot_ids.insert(id(lot.at("id"))).second, "duplicate lot id");
      const auto units=number(lot.at("units"),1,1000000), cost=number(lot.at("unit_cost"));
      require(cost <= limit/units, "lot acquisition value exceeds supported amount");
      inventory=add(inventory,units*cost);
      units_total=add(units_total,units);
    }
  }
  require(product_ids == std::set<std::string>(catalog.begin(),catalog.end()),
          "opening products must exactly match metadata catalog");
  return {{"inventory_value",inventory},{"inventory_units",units_total},
    {"obligations",obligations},{"cash_unreserved",cash-reserve},
    {"cash_after_obligations_and_reserve",cash-reserve-obligations},
    {"net_assets",add(cash,inventory)-obligations}};
}
}
