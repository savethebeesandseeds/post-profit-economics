// SPDX-License-Identifier: MIT
#include "environment.hpp"
#include "../operations/opening.hpp"
#include "../operations/transactions.hpp"
#include "../storage/environment_store.hpp"
#include "../platform/files.hpp"
#include <algorithm>
#include <chrono>
#include <cerrno>
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
  return platform::read_regular_file(path,max);
}
fs::path root_path(const fs::path& root) {
  require(fs::is_directory(root), "environment directory does not exist");
  platform::validate_existing_path(root);
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
// Runtime options deliberately do not become persisted accounting identity.
Json configuration(const fs::path& path, Json& runtime) {
  auto cfg=parse(read_file(path));
  if (cfg.value("schema_version",Json()) == "exchange.environment.v2") {
    fields(cfg,{"schema_version","environment_id","currency","minor_unit_digits",
                "storage","metadata_file","opening_file","runtime"});
    runtime=cfg.at("runtime");
    fields(runtime,{"workspace","server","encryption"});
    text(runtime.at("workspace"));
    const auto& server=runtime.at("server");
    fields(server,{"bind","port"});
    require(server.at("bind") == "127.0.0.1","only loopback publication is supported");
    const auto& port=server.at("port");
    require(port.is_number_integer() && port >= 1024 && port <= 65535,"invalid configured port");
    const auto& encryption=runtime.at("encryption");
    fields(encryption,{"required","key_file"});
    require(encryption.at("required").is_boolean() && encryption.at("required") == true,
            "v2 environments require database encryption");
    text(encryption.at("key_file"),4096);
    cfg.erase("runtime");
    cfg["schema_version"]="exchange.environment.v1";
  } else {
    fields(cfg,{"schema_version","environment_id","currency","minor_unit_digits",
                "storage","metadata_file","opening_file"});
    require(cfg.at("schema_version") == "exchange.environment.v1","unsupported environment schema");
  }
  return cfg;
}
std::string secret(const Json& runtime, const fs::path& path) {
  if (runtime.is_null()) {
    require(path.empty(),"legacy plaintext environment cannot silently enable encryption; migrate explicitly");
    return {};
  }
  require(!path.empty(),"encrypted environment requires --key-file");
  return platform::read_key_file(path);
}
struct Inputs { fs::path root, database, opening; Json config, metadata, runtime; std::vector<std::string> catalog; };
Inputs load(const fs::path& supplied) {
  Inputs in;
  in.root=root_path(supplied);
  in.config=configuration(contained(in.root,"exchange.cfg"),in.runtime);
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
  std::string passphrase;
};
Loaded load_current(const fs::path& root, const fs::path& key_file = {}) {
  Loaded current;
  current.inputs=load(root);
  current.passphrase=secret(current.inputs.runtime,key_file);
  current.payload=storage::read_environment_store(current.inputs.database,current.passphrase);
  current.envelope=parse(current.payload);
  const auto& envelope=current.envelope;
  const bool legacy=envelope.value("schema_version",Json()) == "exchange.environment-store.v1";
  if (legacy) fields(envelope,{"schema_version","configuration","metadata","opening"});
  else {
    fields(envelope,{"schema_version","configuration","metadata","opening","events"});
    require(envelope.at("schema_version") == "exchange.environment-store.v2","unsupported database envelope schema");
  }
  require(envelope.at("configuration") == current.inputs.config && envelope.at("metadata") == current.inputs.metadata,
          "configuration or metadata differs from the initialized environment; records preserved");
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
Json service_configuration(const fs::path& config) {
  require(config.filename() == "exchange.cfg","configuration must be named exchange.cfg");
  const auto in=load(config.parent_path().empty() ? fs::path(".") : config.parent_path());
  require(!in.runtime.is_null(),"--config requires an exchange.environment.v2 configuration");
  return {{"environment_id",in.config.at("environment_id")},{"runtime",in.runtime}};
}
Json migrate(const fs::path& source, const fs::path& destination, const fs::path& key_file) {
  const auto previous=load_current(source);
  require(previous.inputs.runtime.is_null(),"migration source must be a legacy plaintext environment");
  const auto next=load(destination);
  require(next.root != previous.inputs.root,"migration requires a distinct destination");
  require(!next.runtime.is_null(),"migration destination must require encryption");
  const auto passphrase=secret(next.runtime,key_file);
  auto expected_config=previous.inputs.config;
  const auto new_id=next.config.at("environment_id");
  expected_config["environment_id"]=new_id;
  require(expected_config == next.config,"migration may change only the environment identity and runtime configuration");
  auto expected_metadata=previous.inputs.metadata;
  expected_metadata["display_name"]=next.metadata.at("display_name");
  require(expected_metadata == next.metadata,"migration may change only the metadata display name");
  auto envelope=previous.envelope;
  envelope["configuration"]=next.config;
  envelope["metadata"]=next.metadata;
  envelope["opening"]["environment_id"]=new_id;
  if (envelope.contains("events"))
    for (auto& event : envelope["events"]) event["environment_id"]=new_id;
  require(parse(read_file(next.opening)) == envelope.at("opening"),
          "destination opening record must preserve original amounts and change only environment identity");
  // This is deliberately an offline, create-once migration. Never reset, overwrite
  // or delete source data; the launcher must stop its writer before invoking it.
  const auto recovery=contained(next.root,"data/migration-original.idydb");
  require(!fs::exists(fs::symlink_status(next.database)) &&
          !fs::exists(fs::symlink_status(recovery)) && recovery != next.database,
          "migration destination or recovery already exists; preserved");
  require(envelope.dump().size() <= storage::environment_payload_limit,"migrated envelope exceeds limit");
  fs::create_directories(next.database.parent_path());
  storage::create_environment_store(recovery,previous.payload,passphrase);
  require(storage::read_environment_store(recovery,passphrase) == previous.payload,
          "migration recovery verification failed; source preserved");
  storage::create_environment_store(next.database,envelope.dump(),passphrase);
  const auto migrated=load_current(next.root,key_file);
  auto expected_state=previous.state;
  expected_state["environment_id"]=new_id;
  auto expected_history=previous.history;
  for (auto& item : expected_history) item["event"]["environment_id"]=new_id;
  require(migrated.state == expected_state && migrated.history == expected_history,
          "migration changed accounting results; source preserved");
  require(storage::read_environment_store(previous.inputs.database) == previous.payload,
          "source changed during offline migration; all files preserved for inspection");
  return {{"status","migrated"},{"previous_environment_id",previous.inputs.config.at("environment_id")},
          {"recovery_database","data/migration-original.idydb"},
          {"report",report(migrated.inputs,migrated.state,migrated.history)}};
}
Json initialize(const fs::path& root, const fs::path& key_file) {
  const auto in=load(root);
  const auto passphrase=secret(in.runtime,key_file);
  const auto state=parse(read_file(in.opening));
  const auto expected=report(in,state); // All accounting validation precedes persistence.
  (void)expected;
  const Json envelope={{"schema_version","exchange.environment-store.v1"},
                       {"configuration",in.config},{"metadata",in.metadata},{"opening",state}};
  const auto payload=envelope.dump();
  require(payload.size() <= storage::environment_payload_limit,"environment envelope exceeds 60 KiB");
  require(!fs::exists(fs::symlink_status(in.database)),"environment database already exists; preserved");
  fs::create_directories(in.database.parent_path());
  storage::create_environment_store(in.database,payload,passphrase);
  return inspect(root,key_file);
}
Json inspect(const fs::path& root, const fs::path& key_file) {
  const auto current=load_current(root,key_file);
  return report(current.inputs,current.state,current.history);
}
Json record_transaction(const fs::path& root, const std::string& request_text, const fs::path& key_file) {
  const auto request=parse(request_text);
  fields(request,{"expected_revision","event"});
  const auto& revision=request.at("expected_revision");
  require(revision.is_number_integer() && revision >= 0 && revision <= 100,
          "expected_revision must be an integer from 0 to 100");
  const auto& event=request.at("event");
  require(event.is_object() && event.contains("id") && event.at("id").is_string(),
          "transaction must have a text identity");
  const auto current=load_current(root,key_file);
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
    storage::replace_environment_store(current.inputs.database,current.payload,replacement,current.passphrase);
  } catch (const storage::StoreConflict&) {
    // A competing identical commit is a successful retry, not a second event.
    const auto latest=load_current(root,key_file);
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
fs::path render(const fs::path& root, const fs::path& template_path, const fs::path& key_file,
                bool allow_plaintext_export) {
  const auto in=load(root);
  require(in.runtime.is_null() || allow_plaintext_export,
          "encrypted accounts require --allow-plaintext-export to write an unencrypted HTML report");
  const auto snapshot=inspect(root,key_file);
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
  platform::write_report(temporary,destination,page);
  return destination;
}
}
