// SPDX-License-Identifier: MIT
#include "operations/transactions.hpp"
#include "operations/opening.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
using ph::exchange::operations::apply_transaction;
using ph::exchange::operations::opening_summary;
const std::vector<std::string> catalog = {"rice", "soap"};
int checks = 0;
void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
Json opening() {
  return {{"schema_version", "exchange.opening.v1"}, {"environment_id", "vendor"},
          {"as_of", "2026-10-14"}, {"cash", 10000}, {"reserve", 2000},
          {"earned_funding", 300},
          {"obligations", Json::array({{{"id", "old-bill"}, {"kind", "operating"}, {"amount", 1000}}})},
          {"products", Json::array({
            {{"sku", "rice"}, {"published_price", 220}, {"replacement_cost", 180},
             {"lots", Json::array({
               {{"id", "rice-a"}, {"units", 2}, {"unit_cost", 100}},
               {{"id", "rice-b"}, {"units", 3}, {"unit_cost", 150}}
             })}},
            {{"sku", "soap"}, {"published_price", 200}, {"replacement_cost", 100},
             {"lots", Json::array()}}
          })}};
}
Json event(const std::string& kind, Json details, const std::string& id = "event-1") {
  return {{"schema_version", "exchange.transaction.v1"}, {"id", id},
          {"environment_id", "vendor"}, {"date", "2026-10-15"},
          {"kind", kind}, {"details", std::move(details)}};
}
Json delivery() {
  return event("delivery", {{"sku", "rice"}, {"lot_id", "rice-c"}, {"units", 4}, {"unit_cost", 200}});
}
Json sale() { return event("sale", {{"sku", "rice"}, {"units", 4}, {"unit_price", 250}}); }
Json expense(const std::string& settlement = "paid") {
  return event("expense", {{"category", "wages"}, {"description", "Daily wages"},
                          {"amount", 300}, {"settlement", settlement}});
}
Json apply_case(const Json& state, const Json& value) { return apply_transaction(state, value, catalog, "vendor"); }
void rejected(const Json& state, const Json& value, const std::string& label) {
  const auto previous_state = state;
  const auto previous_event = value;
  bool threw = false;
  try { apply_case(state, value); } catch (const std::exception&) { threw = true; }
  require(threw, label + " rejects");
  require(state == previous_state && value == previous_event, label + " preserves inputs");
}
void invalid_event(const std::string& label, Json value) { rejected(opening(), value, label); }
} // namespace

int main() {
  try {
    const auto original = opening();
    const auto received = apply_case(original, delivery());
    require(original == opening(), "successful delivery does not mutate input");
    auto state = received.at("state");
    require(state.at("cash") == 9200 && state.at("earned_funding") == 300,
            "delivery purchases inventory without expensing unsold goods");
    require(received.at("effect") == Json({{"cash_delta", -800}, {"inventory_value_delta", 800},
            {"earned_funding_delta", 0}, {"revenue", 0}, {"cost_of_goods_sold", 0}, {"expense", 0}}),
            "delivery effect reconciles exact acquisition value");
    require(state.at("products")[0].at("lots")[2].at("id") == "rice-c",
            "delivery appends the new FIFO lot");
    require(state.at("schema_version") == "exchange.opening.v1" && state.at("as_of") == "2026-10-15",
            "state remains persistable and advances account date");

    auto sale_record = sale();
    sale_record["id"] = "sale-1";
    const auto sold = apply_case(state, sale_record);
    state = sold.at("state");
    require(sold.at("effect") == Json({{"cash_delta", 1000}, {"inventory_value_delta", -500},
            {"earned_funding_delta", 500}, {"revenue", 1000}, {"cost_of_goods_sold", 500}, {"expense", 0}}),
            "sale uses actual receipt price and FIFO acquisition costs");
    require(state.at("cash") == 10200 && state.at("earned_funding") == 800,
            "sale adds revenue to cash and contribution to earned funding exactly once");
    require(state.at("products")[0].at("lots") == Json::array({
              {{"id", "rice-b"}, {"units", 1}, {"unit_cost", 150}},
              {{"id", "rice-c"}, {"units", 4}, {"unit_cost", 200}}
            }), "sale spans FIFO lots and removes the exhausted lot");
    require(state.at("products")[0].at("published_price") == 220 &&
            state.at("products")[0].at("replacement_cost") == 180,
            "delivery and actual sale leave published price and replacement quote unchanged");

    const auto paid = apply_case(state, expense());
    state = paid.at("state");
    require(state.at("cash") == 9900 && state.at("earned_funding") == 500 &&
            state.at("obligations") == original.at("obligations"),
            "new paid expense reduces cash and earned funding without paying an older obligation");
    auto unpaid_event = expense("unpaid");
    unpaid_event["id"] = "new-bill";
    unpaid_event["details"]["category"] = "operating";
    unpaid_event["details"]["description"] = "Electricity invoice";
    unpaid_event["details"]["amount"] = 400;
    const auto unpaid = apply_case(state, unpaid_event);
    state = unpaid.at("state");
    require(state.at("cash") == 9900 && state.at("earned_funding") == 100,
            "new unpaid expense is accrued once without inventing cash payment");
    require(unpaid.at("effect").at("cash_delta") == 0 &&
            unpaid.at("effect").at("expense") == 400 &&
            unpaid.at("effect").at("earned_funding_delta") == -400,
            "unpaid expense effects distinguish funding from cash");
    require(state.at("obligations")[1] ==
            Json({{"id", "new-bill"}, {"kind", "operating"}, {"amount", 400}}),
            "unpaid obligation retains event identity and expense category");
    const auto summary = opening_summary(state, catalog, "vendor");
    require(summary.at("inventory_value") == 950 && summary.at("inventory_units") == 5 &&
            summary.at("obligations") == 1400 && summary.at("net_assets") == 9450,
            "worked example ends with reconciled inventory, obligations and net assets");
    require(summary.at("cash_after_obligations_and_reserve") == 6500,
            "available cash retains reserve and all unpaid obligations");

    auto all = sale();
    all["details"]["units"] = 5;
    require(apply_case(original, all).at("state").at("products")[0].at("lots").empty(),
            "selling all stock removes every exhausted FIFO lot");
    auto low = sale();
    low["details"]["unit_price"] = 1;
    require(apply_case(original, low).at("effect").at("earned_funding_delta") == -496,
            "observed loss-making sale is recorded honestly without clamping contribution");
    auto paid_from_earmarked_cash = expense();
    paid_from_earmarked_cash["details"]["amount"] = 8000;
    const auto tight = apply_case(original, paid_from_earmarked_cash).at("state");
    require(tight.at("cash") == 2000 && tight.at("obligations") == original.at("obligations") &&
            opening_summary(tight, catalog, "vendor").at("cash_after_obligations_and_reserve") == -1000,
            "paid actual expense protects reserve but exposes unpaid-obligation shortfall");
    auto free_delivery = delivery();
    free_delivery["details"]["unit_cost"] = 0;
    require(apply_case(tight, free_delivery).at("state").at("cash") == 2000,
            "free inventory is acceptable even with an outstanding cash shortfall");
    auto leap = delivery();
    leap["date"] = "2028-02-29";
    require(apply_case(original, leap).at("state").at("as_of") == "2028-02-29", "valid leap date accepted");

    auto bad = delivery(); bad["details"]["units"] = 0; invalid_event("zero units", bad);
    bad = sale(); bad["details"]["units"] = -1; invalid_event("negative units", bad);
    bad = sale(); bad["details"]["units"] = 1.5; invalid_event("fractional units", bad);
    bad = sale(); bad["details"]["units"] = true; invalid_event("boolean units", bad);
    bad = delivery(); bad["details"]["units"] = 1000001; invalid_event("units above supported bound", bad);
    bad = delivery(); bad["details"]["unit_cost"] = -1; invalid_event("negative acquisition cost", bad);
    bad = delivery(); bad["details"]["unit_cost"] = "100"; invalid_event("text amount", bad);
    bad = delivery(); bad["details"]["unit_cost"] = std::numeric_limits<std::uint64_t>::max();
    invalid_event("unsigned monetary overflow before narrowing", bad);
    bad = delivery(); bad["details"]["unit_cost"] = 9000000000000LL;
    invalid_event("delivery multiplication overflow", bad);
    bad = sale(); bad["details"]["unit_price"] = 0; invalid_event("zero receipt price", bad);
    bad = sale(); bad["details"]["unit_price"] = 9000000000000LL;
    invalid_event("sale multiplication overflow", bad);
    bad = sale(); bad["details"]["units"] = 6; invalid_event("insufficient stock", bad);
    bad = delivery(); bad["details"]["sku"] = "not-a-product"; invalid_event("unknown SKU", bad);
    bad = delivery(); bad["details"]["lot_id"] = "rice-a"; invalid_event("duplicate FIFO lot id", bad);
    bad = delivery(); bad["details"]["sku"] = "soap"; bad["details"]["lot_id"] = "rice-a";
    invalid_event("lot identity is unique across products", bad);
    bad = delivery(); bad["details"]["units"] = 36;
    invalid_event("delivery protects existing unpaid obligations", bad);
    bad = delivery(); bad["details"]["units"] = 41;
    invalid_event("delivery cannot consume reserve", bad);
    bad = expense(); bad["details"]["amount"] = 8001; invalid_event("expense cannot consume reserve", bad);
    bad = expense(); bad["details"]["amount"] = 10001; invalid_event("cash underflow", bad);
    bad = expense(); bad["details"]["amount"] = 0; invalid_event("zero expense", bad);
    bad = expense(); bad["details"]["category"] = "owner-dividend"; invalid_event("unsupported expense category", bad);
    bad = expense(); bad["details"]["settlement"] = "pay-old-obligation"; invalid_event("unsupported settlement", bad);
    bad = expense(); bad["details"]["description"] = ""; invalid_event("empty description", bad);
    bad = expense(); bad["details"]["description"] = std::string(513, 'a'); invalid_event("oversized description", bad);
    bad = expense(); bad["details"]["description"] = std::string("a\0b", 3); invalid_event("description NUL", bad);
    bad = expense("unpaid"); bad["id"] = "old-bill"; invalid_event("duplicate obligation id", bad);
    bad = sale(); bad["date"] = "2026-10-13"; invalid_event("backdated transaction", bad);
    bad = sale(); bad["date"] = "2026-02-29"; invalid_event("nonexistent leap date", bad);
    bad = sale(); bad["date"] = "2026-13-01"; invalid_event("invalid month", bad);
    bad = sale(); bad["date"] = "0000-01-01"; invalid_event("invalid year", bad);
    bad = sale(); bad["date"] = "2026-10-15T00:00:00Z"; invalid_event("date must use exact day format", bad);
    bad = sale(); bad["date"] = 20261015; invalid_event("date must be text", bad);
    bad = sale(); bad["environment_id"] = "elsewhere"; invalid_event("environment mismatch", bad);
    bad = sale(); bad["id"] = "../x"; invalid_event("invalid event identifier", bad);
    bad = sale(); bad["schema_version"] = "exchange.transaction.v2"; invalid_event("unknown schema", bad);
    bad = sale(); bad["extra"] = 1; invalid_event("unknown event field", bad);
    bad = sale(); bad["details"]["extra"] = 1; invalid_event("unknown detail field", bad);
    bad = sale(); bad["details"].erase("unit_price"); invalid_event("missing detail field", bad);
    bad = sale(); bad["kind"] = "refund"; invalid_event("unimplemented transaction kind", bad);

    auto bounded = original;
    bounded["earned_funding"] = 9000000000000LL;
    rejected(bounded, sale(), "earned funding positive overflow");
    bounded = original; bounded["earned_funding"] = -9000000000000LL;
    rejected(bounded, expense("unpaid"), "earned funding negative overflow");
    bounded = original; bounded["cash"] = 9000000000000LL - 650;
    rejected(bounded, sale(), "cash overflow");
    bounded = original;
    bounded["obligations"] = Json::array({
      {{"id", "huge-debt"}, {"kind", "operating"}, {"amount", 9000000000000LL}}
    });
    rejected(bounded, expense("unpaid"), "obligation aggregate overflow");

    bounded = original; bounded["products"][1]["lots"] = Json::array();
    for (int i = 0; i < 100; ++i)
      bounded["products"][1]["lots"].push_back({{"id", "soap-" + std::to_string(i)},
                                               {"units", 1}, {"unit_cost", 0}});
    bad = delivery(); bad["details"]["sku"] = "soap";
    rejected(bounded, bad, "FIFO lot count bound");
    bounded = original; bounded["obligations"] = Json::array();
    for (int i = 0; i < 100; ++i)
      bounded["obligations"].push_back({{"id", "bill-" + std::to_string(i)},
                                        {"kind", "operating"}, {"amount", 1}});
    rejected(bounded, expense("unpaid"), "obligation count bound");
    bounded = original; bounded["cash"] = 1;
    rejected(bounded, sale(), "invalid starting state");
    std::cout << checks << " exact transaction accounting checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
