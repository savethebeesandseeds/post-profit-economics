// SPDX-License-Identifier: MIT
#include "interfaces/environment.hpp"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace environment = ph::exchange::environment;
int checks = 0;

void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void rejected(Function action, const std::string& message) {
  bool threw = false;
  try { action(); } catch (const std::exception&) { threw = true; }
  require(threw, message);
}

std::string bytes(const fs::path& file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot read test file: " + file.string());
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write(const fs::path& file, const std::string& content) {
  fs::create_directories(file.parent_path());
  std::ofstream stream(file, std::ios::binary);
  stream << content;
  if (!stream) throw std::runtime_error("cannot write test file: " + file.string());
}
void write(const fs::path& file, const Json& value) { write(file, value.dump(2)); }

Json comparable(Json report) {
  report.erase("snapshot_at");
  return report;
}

struct Fixture {
  fs::path root;
  Json config = {
    {"schema_version", "exchange.environment.v1"},
    {"environment_id", "vendor-example"},
    {"currency", "EUR"}, {"minor_unit_digits", 2},
    {"storage", {{"database", "data/exchange.idydb"}}},
    {"metadata_file", "metadata.json"}, {"opening_file", "records/opening.json"}
  };
  Json metadata = {
    {"schema_version", "exchange.environment-metadata.v1"},
    {"display_name", "Example worker exchange"},
    {"description", "Labeled opening records for a synthetic operation."},
    {"provenance", "synthetic"},
    {"products", Json::array({
      {{"sku", "rice"}, {"label", "Rice"}, {"unit", "piece"}},
      {{"sku", "soap"}, {"label", "Soap"}, {"unit", "piece"}}
    })}
  };
  Json opening = {
    {"schema_version", "exchange.opening.v1"},
    {"environment_id", "vendor-example"}, {"as_of", "2026-10-14"},
    {"cash", 100000}, {"reserve", 5000}, {"earned_funding", 0},
    {"obligations", Json::array({
      {{"id", "pay-1"}, {"kind", "wages"}, {"amount", 10000}},
      {{"id", "rent-1"}, {"kind", "operating"}, {"amount", 2500}}
    })},
    {"products", Json::array({
      {{"sku", "rice"}, {"published_price", 200}, {"replacement_cost", 160},
       {"lots", Json::array({
         {{"id", "rice-1"}, {"units", 10}, {"unit_cost", 120}},
         {{"id", "rice-2"}, {"units", 5}, {"unit_cost", 150}}
       })}},
      {{"sku", "soap"}, {"published_price", 300}, {"replacement_cost", 200},
       {"lots", Json::array({
         {{"id", "soap-1"}, {"units", 4}, {"unit_cost", 200}}
       })}}
    })}
  };

  void save() const {
    write(root / "exchange.cfg", config);
    write(root / "metadata.json", metadata);
    write(root / "records/opening.json", opening);
  }
  fs::path database() const { return root / "data/exchange.idydb"; }
};

template <typename Change>
void invalid_case(const fs::path& base, const std::string& name, Change change) {
  Fixture f{base / name};
  change(f);
  f.save();
  rejected([&] { environment::initialize(f.root); }, name + " rejects initialization");
  require(!fs::exists(f.database()), name + " creates no database");
}
} // namespace

int main() {
  fs::path directory;
  try {
    directory = fs::temp_directory_path() /
      ("exchange-environment-test-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(fs::create_directory(directory), "unique temporary directory created");

    Fixture original{directory / "original"};
    original.save();
    const auto initialized = environment::initialize(original.root);
    require(fs::exists(original.database()), "initialization creates the configured IdyDB");
    require(initialized.at("environment") == original.config, "report preserves full environment config");
    require(initialized.at("metadata") == original.metadata, "report preserves display metadata and provenance");
    require(initialized.at("state") == original.opening, "report preserves exact opening records and FIFO lot order");
    const auto summary = initialized.at("summary");
    require(summary.at("inventory_units") == 19, "stock units include all products and lots");
    require(summary.at("inventory_value") == 2750, "stock valued at acquisition cost, not selling or replacement price");
    require(summary.at("obligations") == 12500, "wage and operating obligations remain separately recorded and sum once");
    require(summary.at("cash_unreserved") == 95000, "reserve restricts available cash without destroying cash");
    require(summary.at("cash_after_obligations_and_reserve") == 82500, "available cash deducts reserve and obligations once");
    require(summary.at("net_assets") == 90250, "net assets include stock and obligations without treating reserve as a loss");
    require(initialized.at("capabilities").at("transactions") == true, "application exposes transaction capability");

    require(comparable(environment::inspect(original.root)) == comparable(initialized),
        "reopening persisted database preserves the entire report");
    const auto database_before = bytes(original.database());
    rejected([&] { environment::initialize(original.root); }, "duplicate initialization refuses overwrite");
    require(bytes(original.database()) == database_before, "duplicate initialization preserves existing database bytes");

    auto changed_opening = original.opening;
    changed_opening["cash"] = 1;
    write(original.root / "records/opening.json", changed_opening);
    require(comparable(environment::inspect(original.root)) == comparable(initialized),
        "editing opening input cannot reset an initialized operation");
    fs::remove(original.root / "records/opening.json");
    require(comparable(environment::inspect(original.root)) == comparable(initialized),
        "persisted operation reopens without its import fixture");

    Fixture alternate{directory / "alternate"};
    alternate.config["environment_id"] = "vendor-west";
    alternate.config["currency"] = "USD";
    alternate.metadata["display_name"] = "West worker collective";
    alternate.metadata["products"] = Json::array({
      {{"sku", "beans"}, {"label", "Beans"}, {"unit", "piece"}}
    });
    alternate.opening["environment_id"] = "vendor-west";
    alternate.opening["earned_funding"] = -300;
    alternate.opening["products"] = Json::array({
      {{"sku", "beans"}, {"published_price", 175}, {"replacement_cost", 90},
       {"lots", Json::array({{{"id", "beans-1"}, {"units", 7}, {"unit_cost", 80}}})}}
    });
    alternate.save();
    const auto other = environment::initialize(alternate.root);
    require(other.at("environment") == alternate.config && other.at("metadata") == alternate.metadata,
        "another operation controls its identity, currency, labels and catalog");
    require(other.at("state") == alternate.opening && other.at("summary").at("inventory_value") == 560,
        "no hardcoded example products; signed earned funding is preserved");

    require(other.at("summary").at("net_assets") == 88060,
        "earned funding is not counted as extra cash or a second liability");

    Fixture large{directory / "large-money"};
    large.opening["cash"] = 8000000000000LL;
    large.opening["reserve"] = 7000000000000LL;
    large.opening["earned_funding"] = -6000000000000LL;
    large.save();
    environment::initialize(large.root);
    const auto large_report = environment::inspect(large.root);
    require(large_report.at("state") == large.opening,
        "64-bit monetary records survive IdyDB close and reopen without narrowing or floating-point conversion");
    require(large_report.at("summary").at("cash_after_obligations_and_reserve") == 999999987500LL,
        "large monetary balances remain exact when reserve and obligations are subtracted");

    Fixture missing{directory / "missing-db"};
    missing.save();
    rejected([&] { environment::inspect(missing.root); }, "inspect rejects an uninitialized operation");
    require(!fs::exists(missing.database()), "inspect does not silently create a database");

    invalid_case(directory, "float-money", [](Fixture& f) { f.opening["cash"] = 100000.0; });
    invalid_case(directory, "boolean-money", [](Fixture& f) { f.opening["cash"] = true; });
    invalid_case(directory, "string-money", [](Fixture& f) { f.opening["cash"] = "100000"; });
    invalid_case(directory, "money-bound", [](Fixture& f) { f.opening["cash"] = 9000000000001LL; });
    invalid_case(directory, "reserve-negative", [](Fixture& f) { f.opening["reserve"] = -1; });
    invalid_case(directory, "reserve-exceeds-cash", [](Fixture& f) { f.opening["reserve"] = 100001; });
    invalid_case(directory, "float-units", [](Fixture& f) { f.opening["products"][0]["lots"][0]["units"] = 1.5; });
    invalid_case(directory, "zero-lot-units", [](Fixture& f) { f.opening["products"][0]["lots"][0]["units"] = 0; });
    invalid_case(directory, "units-bound", [](Fixture& f) { f.opening["products"][0]["lots"][0]["units"] = 1000001; });
    invalid_case(directory, "nonpositive-price", [](Fixture& f) { f.opening["products"][0]["published_price"] = 0; });
    invalid_case(directory, "negative-cost", [](Fixture& f) { f.opening["products"][0]["lots"][0]["unit_cost"] = -1; });
    invalid_case(directory, "inventory-value-bound", [](Fixture& f) {
      f.opening["products"][0]["lots"][0]["units"] = 1000000;
      f.opening["products"][0]["lots"][0]["unit_cost"] = 9000000000000LL;
    });
    invalid_case(directory, "duplicate-catalog-sku", [](Fixture& f) {
      f.metadata["products"].push_back(f.metadata["products"][0]);
    });
    invalid_case(directory, "duplicate-stock-sku", [](Fixture& f) {
      f.opening["products"].push_back(f.opening["products"][0]);
    });
    invalid_case(directory, "unknown-stock-sku", [](Fixture& f) { f.opening["products"][0]["sku"] = "not-in-catalog"; });
    invalid_case(directory, "wrong-operation-id", [](Fixture& f) { f.opening["environment_id"] = "some-other-operation"; });
    invalid_case(directory, "unknown-schema", [](Fixture& f) { f.config["schema_version"] = "exchange.environment.v99"; });
    invalid_case(directory, "unsupported-unit", [](Fixture& f) { f.metadata["products"][0]["unit"] = "kg"; });
    invalid_case(directory, "invalid-provenance", [](Fixture& f) { f.metadata["provenance"] = "probably-observed"; });
    invalid_case(directory, "database-traversal", [](Fixture& f) { f.config["storage"]["database"] = "../outside.idydb"; });
    invalid_case(directory, "metadata-traversal", [](Fixture& f) { f.config["metadata_file"] = "../metadata.json"; });
    invalid_case(directory, "opening-traversal", [](Fixture& f) { f.config["opening_file"] = "../opening.json"; });
    invalid_case(directory, "metadata-under-database", [](Fixture& f) { f.config["metadata_file"] = "data/metadata.json"; });
    invalid_case(directory, "opening-under-reports", [](Fixture& f) { f.config["opening_file"] = "reports/opening.json"; });

    Fixture duplicate_key{directory / "duplicate-json-key"};
    duplicate_key.save();
    auto raw_config = duplicate_key.config.dump();
    raw_config.insert(1, "\"currency\":\"EUR\",");
    write(duplicate_key.root / "exchange.cfg", raw_config);
    rejected([&] { environment::initialize(duplicate_key.root); }, "duplicate JSON keys are rejected");
    require(!fs::exists(duplicate_key.database()), "duplicate keys produce no database");

    Fixture altered{directory / "altered-records"};
    altered.save();
    environment::initialize(altered.root);
    auto changed_config = altered.config;
    changed_config["currency"] = "USD";
    write(altered.root / "exchange.cfg", changed_config);
    rejected([&] { environment::inspect(altered.root); }, "configuration changes cannot reinterpret persisted amounts");
    write(altered.root / "exchange.cfg", altered.config);
    auto changed_metadata = altered.metadata;
    changed_metadata["provenance"] = "observed";
    write(altered.root / "metadata.json", changed_metadata);
    rejected([&] { environment::inspect(altered.root); }, "synthetic records cannot silently become observed records");
    write(altered.root / "metadata.json", altered.metadata);
    require(environment::inspect(altered.root).at("state") == altered.opening, "rejected metadata/config edits preserve persisted records");

    const auto outside = directory / "outside";
    fs::create_directory(outside);
    write(outside / "marker", std::string("preserve"));
    Fixture linked_data{directory / "linked-data"};
    linked_data.save();
    fs::create_directory_symlink(outside, linked_data.root / "data");
    rejected([&] { environment::initialize(linked_data.root); }, "database directory symlink cannot escape environment");
    require(!fs::exists(outside / "exchange.idydb") && bytes(outside / "marker") == "preserve",
        "rejected symlink leaves external files untouched");
    Fixture linked_metadata{directory / "linked-metadata"};
    linked_metadata.save();
    write(outside / "metadata.json", linked_metadata.metadata);
    fs::remove(linked_metadata.root / "metadata.json");
    fs::create_symlink(outside / "metadata.json", linked_metadata.root / "metadata.json");
    rejected([&] { environment::initialize(linked_metadata.root); }, "metadata symlink outside environment is rejected");
    require(!fs::exists(linked_metadata.database()), "escaped metadata produces no database");

    Fixture html_fixture{directory / "html"};
    html_fixture.metadata["display_name"] = "</script><script>alert('fixture')</script> & >";
    html_fixture.save();
    const auto html_report = environment::initialize(html_fixture.root);
    const auto template_path = directory / "report.template.html";
    const std::string template_text =
        "<!doctype html><title>Environment</title><script type=\"application/json\" id=\"environment-data\">"
        "__ENVIRONMENT_JSON__</script>";
    write(template_path, template_text);
    const auto rendered = environment::render(html_fixture.root, template_path);
    require(rendered == html_fixture.root / "reports/environment.html", "report is generated beneath its environment");
    const auto html = bytes(rendered);
    const auto begin = html.find("id=\"environment-data\">") + std::string("id=\"environment-data\">").size();
    const auto end = html.find("</script>", begin);
    require(end != std::string::npos, "template retains its data-script boundary");
    const auto encoded = html.substr(begin, end - begin);
    require(encoded.find('<') == std::string::npos && encoded.find('>') == std::string::npos &&
            encoded.find('&') == std::string::npos, "JSON embedded in HTML escapes markup-significant characters");
    require(html.find("<script>alert") == std::string::npos, "metadata cannot inject executable script markup");
    require(comparable(Json::parse(encoded)) == comparable(html_report), "escaped HTML preserves all data for rendering");
    environment::render(html_fixture.root, template_path);
    require(comparable(Json::parse(bytes(rendered).substr(begin, bytes(rendered).find("</script>", begin) - begin))) ==
            comparable(html_report), "generated report can be safely refreshed");

    const auto html_before = bytes(rendered);
    write(template_path, std::string("No JSON placeholder"));
    rejected([&] { environment::render(html_fixture.root, template_path); }, "missing template placeholder is rejected");
    require(bytes(rendered) == html_before, "invalid template preserves existing report");
    write(template_path, std::string("__ENVIRONMENT_JSON__ __ENVIRONMENT_JSON__"));
    rejected([&] { environment::render(html_fixture.root, template_path); }, "duplicate template placeholder is rejected");
    require(bytes(rendered) == html_before, "duplicate placeholder preserves existing report");

    write(template_path, template_text);
    fs::remove(rendered);
    fs::create_symlink(outside / "marker", rendered);
    rejected([&] { environment::render(html_fixture.root, template_path); }, "existing report symlink cannot overwrite another file");
    require(bytes(outside / "marker") == "preserve", "report symlink leaves its target untouched");
    fs::remove(rendered);
    fs::remove(html_fixture.root / "reports");
    fs::create_directory_symlink(outside, html_fixture.root / "reports");
    rejected([&] { environment::render(html_fixture.root, template_path); }, "report directory symlink cannot escape environment");
    require(!fs::exists(outside / "environment.html"), "report directory symlink creates no external report");

    std::cout << checks << " environment checks passed; fixtures preserved at " << directory << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "; fixtures preserved at " << directory << '\n';
    return 1;
  }
}
