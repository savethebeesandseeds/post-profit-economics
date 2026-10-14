// SPDX-License-Identifier: MIT
#include "environment.hpp"
#include "../operations/opening.hpp"
#include "../operations/transactions.hpp"
#include "../storage/environment_store.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace ph::exchange::environment {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t input_limit = 60 * 1024;
void require(bool ok, const std::string& message) {
  if (!ok) throw std::invalid_argument(message);
}
void fields(const Json& v, std::initializer_list<const char*> keys) {
  require(v.is_object() && v.size() == keys.size(), "environment record has missing or unknown fields");
  for (const auto* k : keys) require(v.contains(k), std::string("environment record missing ") + k);
}
std::string text(const Json& v, std::size_t max=1024) {
  require(v.is_string(), "environment text field must be a string");
  auto s=v.get<std::string>();
  require(!s.empty() && s.size() <= max && std::all_of(s.begin(),s.end(),[](unsigned char c) {
    return c >= 32 && c != 127;
  }), "environment text is empty, too long or contains control characters");
  return s;
}
std::string identifier(const Json& v) {
  auto s=text(v,64);
  require(std::all_of(s.begin(),s.end(),[](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
  }), "invalid environment identifier or SKU");
  return s;
}
Json parse(const std::string& bytes) {
  require(bytes.size() <= input_limit, "environment JSON exceeds 60 KiB");
  std::vector<std::set<std::string>> keys;
  return Json::parse(bytes,[&](int, Json::parse_event_t event, Json& value) {
    if (event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start) {
      keys.emplace_back();
      require(keys.size() <= 32, "environment JSON nesting exceeds 32");
    } else if (event == Json::parse_event_t::key) {
      require(keys.back().insert(value.get<std::string>()).second, "duplicate JSON key");
    } else if (event == Json::parse_event_t::object_end || event == Json::parse_event_t::array_end) {
      keys.pop_back();
    }
    return true;
  });
}
std::string read_file(const fs::path& path, std::size_t max=input_limit) {
  require(fs::is_regular_file(fs::symlink_status(path)), "expected a regular file: " + path.string());
  require(fs::file_size(path) <= max, "file exceeds supported size: " + path.string());
  std::ifstream in(path,std::ios::binary);
  require(bool(in), "cannot open " + path.string());
  std::string bytes;
  char buffer[4096];
  while (in) {
    in.read(buffer,sizeof buffer);
    bytes.append(buffer,static_cast<std::size_t>(in.gcount()));
    require(bytes.size() <= max,"file grew beyond supported size");
  }
  require(in.eof(),"cannot finish reading " + path.string());
  return bytes;
}
fs::path root_path(const fs::path& root) {
  require(fs::is_directory(root), "environment directory does not exist");
  return fs::canonical(root);
}
fs::path contained(const fs::path& root, const std::string& relative) {
  const fs::path path=fs::u8path(relative);
  require(!path.empty() && !path.is_absolute() && relative.find(':') == std::string::npos &&
          relative.find('\\') == std::string::npos, "environment path must be relative to its directory");
  auto full=root;
  for (const auto& part : path) {
    require(part != "." && part != ".." && !part.empty(),"environment path traversal is not allowed");
    full /= part;
    require(!fs::is_symlink(fs::symlink_status(full)), "environment paths cannot traverse symlinks");
  }
  return full;
}
struct Inputs { fs::path root, database, opening; Json config, metadata; std::vector<std::string> catalog; };
Inputs load(const fs::path& supplied) {
  Inputs in;
  in.root=root_path(supplied);
  in.config=parse(read_file(contained(in.root,"exchange.cfg")));
  fields(in.config,{"schema_version","environment_id","currency","minor_unit_digits",
                    "storage","metadata_file","opening_file"});
  require(in.config.at("schema_version") == "exchange.environment.v1","unsupported environment schema");
  identifier(in.config.at("environment_id"));
  auto currency=text(in.config.at("currency"),3);
  require(currency.size() == 3 && std::all_of(currency.begin(),currency.end(),[](char c) {
    return c >= 'A' && c <= 'Z';
  }),"currency must be a three-letter uppercase code");
  require(in.config.at("minor_unit_digits").is_number_integer() &&
          in.config.at("minor_unit_digits") == 2,"this environment version supports two decimal currency units");
  fields(in.config.at("storage"),{"database"});
  auto database=text(in.config.at("storage").at("database"));
  require(database.rfind("data/",0) == 0 && fs::path(database).extension() == ".idydb",
          "database must be a .idydb file below data/");
  in.database=contained(in.root,database);
  auto meta=text(in.config.at("metadata_file")), opening=text(in.config.at("opening_file"));
  require(meta != opening && meta != "exchange.cfg" && opening != "exchange.cfg",
          "environment input paths must be distinct");
  for (const auto& path : {meta,opening}) {
    require(path.rfind("data/",0) != 0 && path.rfind("reports/",0) != 0,
            "environment input files cannot be below data/ or reports/");
  }
  in.metadata=parse(read_file(contained(in.root,meta)));
  in.opening=contained(in.root,opening);
  fields(in.metadata,{"schema_version","display_name","description","provenance","products"});
  require(in.metadata.at("schema_version") == "exchange.environment-metadata.v1","unsupported metadata schema");
  text(in.metadata.at("display_name"),160); text(in.metadata.at("description"),4096);
  require(in.metadata.at("provenance") == "synthetic" || in.metadata.at("provenance") == "observed",
          "metadata provenance must be synthetic or observed");
  const auto& products=in.metadata.at("products");
  require(products.is_array() && !products.empty() && products.size() <= 12,"metadata requires 1 to 12 products");
  std::set<std::string> skus;
  for (const auto& p : products) {
    fields(p,{"sku","label","unit"});
    auto sku=identifier(p.at("sku"));
    require(skus.insert(sku).second,"duplicate metadata SKU");
    text(p.at("label"),160);
    require(p.at("unit") == "piece","this environment version supports whole pieces");
    in.catalog.push_back(sku);
  }
  return in;
}
Json report(const Inputs& in, const Json& state, const Json& events = Json::array()) {
  const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  const auto utc=*std::gmtime(&now);
  std::ostringstream stamp;
  stamp << std::put_time(&utc,"%Y-%m-%dT%H:%M:%SZ");
  return {{"schema_version","exchange.environment-view.v1"},
    {"environment",in.config},{"metadata",in.metadata},{"state",state},
    {"summary",operations::opening_summary(state,in.catalog,in.config.at("environment_id"))},
    {"revision",events.size()},{"events",events},
    {"capabilities",{{"opening_state",true},{"transactions",true},
                     {"price_recommendations",false},{"price_publication",false}}},
    {"database",{{"backend","idydb"},{"status","loaded"}}},{"snapshot_at",stamp.str()}};
}
struct Loaded {
  Inputs inputs;
  std::string payload;
  Json envelope, state, history;
};
Loaded load_current(const fs::path& root) {
  Loaded current;
  current.inputs=load(root);
  current.payload=storage::read_environment_store(current.inputs.database);
  current.envelope=parse(current.payload);
  const auto& envelope=current.envelope;
  const bool legacy=envelope.value("schema_version",Json()) == "exchange.environment-store.v1";
  if (legacy) fields(envelope,{"schema_version","configuration","metadata","opening"});
  else {
    fields(envelope,{"schema_version","configuration","metadata","opening","events"});
    require(envelope.at("schema_version") == "exchange.environment-store.v2","unsupported database envelope schema");
  }
  require(envelope.at("configuration") == current.inputs.config && envelope.at("metadata") == current.inputs.metadata,
          "configuration or metadata differs from the initialized environment; migration is not implemented");
  current.state=envelope.at("opening");
  operations::opening_summary(current.state,current.inputs.catalog,current.inputs.config.at("environment_id"));
  current.history=Json::array();
  std::set<std::string> ids, lots;
  for (const auto& product : current.state.at("products"))
    for (const auto& lot : product.at("lots")) lots.insert(lot.at("id").get<std::string>());
  if (!legacy) {
    const auto& events=envelope.at("events");
    require(events.is_array() && events.size() <= 100,"environment supports at most 100 transactions");
    for (const auto& event : events) {
      require(event.is_object() && event.contains("id") && event.at("id").is_string(),
              "stored transaction has no identity");
      require(ids.insert(event.at("id").get<std::string>()).second,"duplicate stored transaction identity");
      if (event.value("kind","") == "delivery") {
        const auto lot=event.at("details").at("lot_id").get<std::string>();
        require(lots.insert(lot).second,"delivery lot identity has already been used");
      }
      const auto applied=operations::apply_transaction(current.state,event,current.inputs.catalog,
                                                       current.inputs.config.at("environment_id"));
      current.state=applied.at("state");
      current.history.push_back({{"event",event},{"effect",applied.at("effect")}});
    }
  }
  return current;
}
void replace_all(std::string& s, const std::string& from, const std::string& to) {
  std::size_t pos=0;
  while ((pos=s.find(from,pos)) != std::string::npos) { s.replace(pos,from.size(),to); pos+=to.size(); }
}
}
Json initialize(const fs::path& root) {
  const auto in=load(root);
  const auto state=parse(read_file(in.opening));
  const auto expected=report(in,state); // All accounting validation precedes persistence.
  (void)expected;
  const Json envelope={{"schema_version","exchange.environment-store.v1"},
                       {"configuration",in.config},{"metadata",in.metadata},{"opening",state}};
  const auto payload=envelope.dump();
  require(payload.size() <= storage::environment_payload_limit,"environment envelope exceeds 60 KiB");
  require(!fs::exists(fs::symlink_status(in.database)),"environment database already exists; preserved");
  fs::create_directories(in.database.parent_path());
  storage::create_environment_store(in.database,payload);
  return inspect(root);
}
Json inspect(const fs::path& root) {
  const auto current=load_current(root);
  return report(current.inputs,current.state,current.history);
}
Json record_transaction(const fs::path& root, const std::string& request_text) {
  const auto request=parse(request_text);
  fields(request,{"expected_revision","event"});
  const auto& revision=request.at("expected_revision");
  require(revision.is_number_integer() && revision >= 0 && revision <= 100,
          "expected_revision must be an integer from 0 to 100");
  const auto& event=request.at("event");
  require(event.is_object() && event.contains("id") && event.at("id").is_string(),
          "transaction must have a text identity");
  const auto current=load_current(root);
  for (const auto& item : current.history) {
    if (item.at("event").at("id") == event.at("id")) {
      if (item.at("event") != event) throw Conflict("transaction identity already used with different content");
      auto result=report(current.inputs,current.state,current.history);
      result["commit_status"]="already_recorded";
      return result;
    }
  }
  if (revision.get<std::size_t>() != current.history.size())
    throw Conflict("accounts changed since this form was loaded; refresh and review before submitting");
  require(current.history.size() < 100,"environment transaction limit reached; records preserved");
  auto next=current.envelope;
  next["schema_version"]="exchange.environment-store.v2";
  if (!next.contains("events")) next["events"]=Json::array();
  if (event.value("kind","") == "delivery") {
    const auto lot=event.at("details").at("lot_id");
    for (const auto& p : next.at("opening").at("products"))
      for (const auto& l : p.at("lots"))
        require(l.at("id") != lot,"delivery lot identity has already been used");
    for (const auto& past : next.at("events"))
      if (past.at("kind") == "delivery")
        require(past.at("details").at("lot_id") != lot,"delivery lot identity has already been used");
  }
  const auto applied=operations::apply_transaction(current.state,event,current.inputs.catalog,
                                                    current.inputs.config.at("environment_id"));
  next["events"].push_back(event);
  const auto replacement=next.dump();
  require(replacement.size() <= storage::environment_payload_limit,"environment envelope exceeds 60 KiB; records preserved");
  try {
    storage::replace_environment_store(current.inputs.database,current.payload,replacement);
  } catch (const storage::StoreConflict&) {
    // A competing identical commit is a successful retry, not a second event.
    const auto latest=load_current(root);
    for (const auto& item : latest.history) {
      if (item.at("event") == event) {
        auto result=report(latest.inputs,latest.state,latest.history);
        result["commit_status"]="already_recorded";
        return result;
      }
    }
    throw Conflict("accounts changed concurrently; refresh and review before submitting");
  }
  auto history=current.history;
  history.push_back({{"event",event},{"effect",applied.at("effect")}});
  auto result=report(current.inputs,applied.at("state"),history);
  result["commit_status"]="recorded";
  return result;
}
fs::path render(const fs::path& root, const fs::path& template_path) {
  const auto snapshot=inspect(root);
  auto page=read_file(template_path,512*1024);
  const std::string token="__ENVIRONMENT_JSON__";
  const auto pos=page.find(token);
  require(pos != std::string::npos && page.find(token,pos+token.size()) == std::string::npos,
          "environment template must contain exactly one data token");
  auto data=snapshot.dump();
  replace_all(data,"&","\\u0026"); replace_all(data,"<","\\u003c"); replace_all(data,">","\\u003e");
  page.replace(pos,token.size(),data);
  auto directory=root_path(root);
  const auto destination=contained(directory,"reports/environment.html");
  const auto temporary=contained(directory,"reports/environment.html.pending");
  require(!fs::exists(destination) || fs::is_regular_file(fs::symlink_status(destination)),
          "report destination must be a regular generated file");
  fs::create_directories(destination.parent_path());
  FILE* out=std::fopen(temporary.c_str(),"wbx");
  require(out != nullptr,"pending report already exists or cannot be created; preserved for inspection");
  bool written=std::fwrite(page.data(),1,page.size(),out) == page.size();
  bool closed=std::fclose(out) == 0;
  require(written && closed,"report write failed; pending file preserved");
  fs::rename(temporary,destination);
  return destination;
}
}
