// SPDX-License-Identifier: MIT
// HTTP transport only: environment APIs retain validation, accounting and storage.
#include "environment.hpp"
#include "server_platform.hpp"
#ifdef _WIN32
#include "../platform/files.hpp"
#endif
#include <httplib.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <memory>
#include <charconv>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace environment = ph::exchange::environment;
namespace server_platform = ph::exchange::server_platform;
using Json = environment::Json;
using Response = httplib::Response;
using Request = httplib::Request;
constexpr std::size_t request_limit = 16384;
constexpr auto operation_timeout = std::chrono::seconds(20);
struct Reply { int status; Json body; };
Reply error(int status, const std::string& message) {
  return {status, {{"status","error"}, {"message",message}}};
}
void send(Response& response, const Reply& reply) {
  response.status = reply.status;
  response.set_content(reply.body.dump(), "application/json; charset=utf-8");
}
Reply execute(const fs::path& root, const fs::path& key_file, bool write, const std::string& body) {
  try {
    return {200, write ? environment::record_transaction(root, body, key_file) : environment::inspect(root, key_file)};
  } catch (const environment::Conflict& e) {
    return {409, {{"status","error"},{"error_type","conflict"},
                  {"commit_status","not_recorded"},{"message",e.what()}}};
  } catch (const std::invalid_argument& e) {
    return {400, {{"status","error"},{"error_type","validation"},{"message",e.what()}}};
  } catch (const Json::exception& e) {
    return {400, {{"status","error"},{"error_type","validation"},{"message",e.what()}}};
  } catch (const std::exception&) {
    // A storage failure may follow publication. Never imply rollback or log records.
    return error(500, "The application did not confirm completion. Retry the same submission to check its status.");
  }
}

// Serialize the native APIs, including timestamp conversion. A timeout releases
// the HTTP thread, never cancels a possible commit. Jobs own their input.
class Accounts {
  struct Job { bool write; std::string body; std::promise<Reply> result; };
  fs::path root_, key_file_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::unique_ptr<Job>> jobs_;
  bool stopping_ = false;
  std::thread worker_;
  void work() {
    for (;;) {
      std::unique_ptr<Job> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty()) return;
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      job->result.set_value(execute(root_, key_file_, job->write, job->body));
    }
  }
 public:
  explicit Accounts(fs::path root, fs::path key_file) : root_(std::move(root)), key_file_(std::move(key_file)), worker_([this] { work(); }) {}
  ~Accounts() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    ready_.notify_one();
    worker_.join();
  }
  Reply request(bool write, std::string body = {}) {
    auto job = std::make_unique<Job>();
    job->write = write;
    job->body = std::move(body);
    auto result = job->result.get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (jobs_.size() >= 4)
        return error(503, "The application is busy. Retry the same submission to check its status.");
      jobs_.push_back(std::move(job));
    }
    ready_.notify_one();
    if (result.wait_for(operation_timeout) != std::future_status::ready)
      return error(500, "The application did not confirm completion. Retry the same submission to check its status.");
    return result.get();
  }
};

bool same_token(const std::string& supplied, const std::string& expected) {
  if (supplied.size() != expected.size()) return false;
  volatile unsigned int difference = 0;
  for (std::size_t i = 0; i < expected.size(); ++i)
    difference |= static_cast<unsigned char>(supplied[i]) ^ static_cast<unsigned char>(expected[i]);
  return difference == 0;
}
std::string page_bytes(const fs::path& path) {
  constexpr std::size_t limit = 2 * 1024 * 1024;
#ifdef _WIN32
  return ph::exchange::platform::read_regular_file(path,limit);
#else
  if (!fs::is_regular_file(fs::symlink_status(path)) || fs::file_size(path) > limit)
    throw std::invalid_argument("application page must be a regular file of at most 2 MiB");
  std::ifstream input(path, std::ios::binary);
  std::string result;
  char buffer[4096];
  while (input) {
    input.read(buffer,sizeof buffer);
    result.append(buffer,static_cast<std::size_t>(input.gcount()));
    if (result.size() > limit) throw std::invalid_argument("application page exceeds 2 MiB");
  }
  if (!input.eof()) throw std::invalid_argument("cannot read application page");
  return result;
#endif
}
std::string media_type(std::string value) {
  value = value.substr(0,value.find(';'));
  const auto begin = value.find_first_not_of(" \t"), end = value.find_last_not_of(" \t");
  if (begin == std::string::npos) return {};
  value = value.substr(begin, end-begin+1);
  for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
  return value;
}
bool length(const std::string& text, std::uint64_t& n) {
  if (text.empty()) return false;
  auto parsed = std::from_chars(text.data(), text.data()+text.size(), n);
  return parsed.ec == std::errc() && parsed.ptr == text.data()+text.size();
}
struct Options {
  fs::path root, page, config, key_file;
  std::string id, bind = "127.0.0.1";
  int port = 8766;
#ifdef _WIN32
  bool browser = true;
#endif
};
Options options(const std::vector<std::string>& arguments) {
  Options o;
  std::set<std::string> seen;
  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const auto& key = arguments[i];
    if (!seen.insert(key).second) throw std::invalid_argument("repeated server option");
#ifdef _WIN32
    if (key == "--no-browser") { o.browser = false; continue; }
#endif
    if (++i == arguments.size()) throw std::invalid_argument("missing server option value");
    const auto& value = arguments[i];
    if (key == "--config") o.config = fs::u8path(value);
    else if (key == "--page") o.page = fs::u8path(value);
    else if (key == "--bind") o.bind = value;
#ifndef _WIN32
    else if (key == "--environment") o.root = fs::u8path(value);
    else if (key == "--environment-id") o.id = value;
    else if (key == "--key-file") o.key_file = fs::u8path(value);
    else if (key == "--port") {
      std::uint64_t port = 0;
      if (!length(value,port) || port > 65535) throw std::invalid_argument("invalid server port");
      o.port = static_cast<int>(port);
    }
#endif
    else throw std::invalid_argument("unknown server option");
  }
#ifdef _WIN32
  if (o.config.empty()) throw std::invalid_argument("Windows application requires --config");
  if (o.page.empty()) o.page = server_platform::executable_path().parent_path() / L"web/transactions.html";
#endif
  if (!o.config.empty()) {
    if (seen.count("--environment") || seen.count("--environment-id") || seen.count("--port"))
      throw std::invalid_argument("--config replaces environment, environment-id and port flags");
    o.config = fs::absolute(o.config).lexically_normal();
    const auto configuration=environment::service_configuration(o.config);
    o.root=o.config.parent_path();
    o.id=configuration.at("environment_id").get<std::string>();
    const auto& runtime=configuration.at("runtime");
    o.port=runtime.at("server").at("port").get<int>();
    if (!seen.count("--bind")) o.bind=runtime.at("server").at("bind").get<std::string>();
#ifdef _WIN32
    o.key_file=ph::exchange::platform::key_file_for_config(
        o.config, runtime.at("workspace").get<std::string>(),
        runtime.at("encryption").at("key_file").get<std::string>());
#endif
  }
#ifdef _WIN32
  if (o.bind != "127.0.0.1") throw std::invalid_argument("Windows application binds only to loopback");
#endif
  if (o.root.empty() || o.page.empty() || o.id.empty() || o.id.size() > 48 ||
      (o.bind != "127.0.0.1" && o.bind != "0.0.0.0"))
    throw std::invalid_argument("environment, environment-id, page and a supported bind address are required");
  return o;
}

// Exception-safe after the listener starts: even a broken readiness pipe stops
// and joins it before stack unwinding reaches the account worker.
class Listener {
  httplib::Server& server_;
  std::thread thread_;
 public:
  Listener(httplib::Server& server, std::packaged_task<bool()> listen)
      : server_(server), thread_(std::move(listen)) {}
  ~Listener() { server_.stop(); if (thread_.joinable()) thread_.join(); }
  void join() { thread_.join(); }
};
}  // namespace

int run(const std::vector<std::string>& arguments) {
  if (arguments.size() == 2 && arguments[1] == "--help") {
#ifdef _WIN32
    std::cout << "post-profit-exchange.exe --config PATH [--no-browser]\n"
                 "Starts the local application and opens its browser interface.\n"
                 "The configuration supplies the environment, port and encryption key file.\n"
                 "Keep this console open. Press Ctrl+C to stop after pending work completes.\n";
#else
    std::cout << "exchange_server --config exchange.cfg --key-file PATH --page HTML [--bind 0.0.0.0]\n"
                 "exchange_server --environment DIRECTORY --environment-id ID --page HTML "
                 "[--bind 127.0.0.1|0.0.0.0] [--port 8766]\n"
                 "Opens existing records only. Port 0 selects an ephemeral port for tests.\n";
#endif
    return 0;
  }
  try {
    const auto opts = options(arguments);
    server_platform::Shutdown shutdown;
    server_platform::StartupOutput output;
    const auto root = fs::canonical(opts.root);
    const auto html = page_bytes(opts.page);
    const auto initial = environment::inspect(root,opts.key_file);
    if (initial.at("environment").at("environment_id") != opts.id)
      throw std::invalid_argument("environment identity differs from the configured service");
    const auto token = server_platform::session_token();
    Accounts accounts(root,opts.key_file);
    httplib::Server server;
    // Local transport only: no TLS, compression, directory mount or trusted proxy.
    server.new_task_queue = [] { return new httplib::ThreadPool(4,4,8); };
    server.set_payload_max_length(request_limit);
    server.set_read_timeout(10);
    server.set_write_timeout(10);
    server.set_keep_alive_max_count(1);
    server.set_socket_options([](socket_t socket) {
      int enabled = 1;
#ifdef _WIN32
      // SO_REUSEADDR on Windows permits competing listeners. Reserve this
      // address exclusively so opening a second instance must fail.
      if (::setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                        reinterpret_cast<const char*>(&enabled), sizeof enabled) != 0)
        throw std::runtime_error("cannot reserve the application port");
#else
      ::setsockopt(socket,SOL_SOCKET,SO_REUSEADDR,&enabled,sizeof enabled);
      // No SO_REUSEPORT: another listener must cause launch to fail.
#endif
    });
    server.set_default_headers({
      {"Cache-Control","no-store"}, {"X-Content-Type-Options","nosniff"},
      {"X-Frame-Options","DENY"}, {"Referrer-Policy","no-referrer"},
      {"Connection","close"}, {"Server","ExchangeLocal/2"}
    });
    server.set_error_handler([](const Request&,Response& response) {
      if (response.body.empty()) send(response,error(response.status,"The application could not accept this request."));
    });
    server.set_exception_handler([](const Request&,Response& response,std::exception_ptr) {
      send(response,error(500,"The application did not confirm completion. Retry the same submission to check its status."));
    });
    int bound_port = opts.port;
    server.set_pre_routing_handler([&](const Request& request,Response& response) {
      const auto reject = [&](int status,const std::string& message) {
        send(response,error(status,message));
        return httplib::Server::HandlerResponse::Handled;
      };
      const auto host = request.get_header_value("Host");
      if (request.get_header_value_count("Host") != 1 ||
          (host != "127.0.0.1:"+std::to_string(bound_port) && host != "localhost:"+std::to_string(bound_port)))
        return reject(403,"Use the configured local application URL.");
      if (request.get_header_value_count("Origin") > 1 ||
          (request.has_header("Origin") && request.get_header_value("Origin") != "http://"+host))
        return reject(403,"Cross-origin requests are not accepted.");
      if (request.get_header_value_count("Sec-Fetch-Site") > 1 || request.get_header_value("Sec-Fetch-Site") == "cross-site")
        return reject(403,"Cross-site requests are not accepted.");
      if (request.method != "GET" && request.method != "HEAD" && request.method != "POST")
        return reject(405,"This request method is unavailable.");
      const bool write = request.method == "POST";
      if ((write && request.path != "/api/transactions") ||
          (!write && request.path != "/" && request.path != "/api/state" && request.path != "/api/session"))
        return reject(404,"Unknown application route.");
      if (write && (request.get_header_value_count("X-Exchange-Token") != 1 ||
                    !same_token(request.get_header_value("X-Exchange-Token"),token)))
        return reject(403,"This application session is not current. Reload before recording.");
      if (request.has_header("Transfer-Encoding"))
        return reject(400,"Chunked requests are not supported.");
      if (request.has_header("Content-Encoding"))
        return reject(415,"Encoded request bodies are not supported.");
      const auto count = request.get_header_value_count("Content-Length");
      std::uint64_t size = 0;
      if ((write && count != 1) || count > 1 ||
          (count && !length(request.get_header_value("Content-Length"),size)))
        return reject(400,"One valid request length is required.");
      if (write && (size == 0 || size > request_limit))
        return reject(413,"Transaction request must contain 1 to 16384 bytes.");
      if (!write && size != 0) return reject(400,"Read requests cannot contain a body.");
      if (write && (request.get_header_value_count("Content-Type") != 1 ||
                    media_type(request.get_header_value("Content-Type")) != "application/json"))
        return reject(415,"Transactions must use application/json.");
      return httplib::Server::HandlerResponse::Unhandled;
    });
    server.Get("/",[&](const Request&,Response& response) {
      response.set_content(html,"text/html; charset=utf-8");
    });
    server.Get("/api/session",[&](const Request&,Response& response) {
      send(response,{200,{{"token",token}}});
    });
    server.Get("/api/state",[&](const Request&,Response& response) {
      send(response,accounts.request(false));
    });
    server.Post("/api/transactions",[&](const Request& request,Response& response) {
      // Preserve raw input so native validation can reject duplicate JSON keys.
      send(response,accounts.request(true,request.body));
    });
    if (opts.port == 0) bound_port = server.bind_to_any_port(opts.bind);
    else if (!server.bind_to_port(opts.bind,opts.port)) bound_port = -1;
    if (bound_port < 0) throw std::runtime_error("cannot bind the configured application port");
    std::packaged_task<bool()> listen([&] { return server.listen_after_bind(); });
    auto done = listen.get_future();
    Listener listener(server,std::move(listen));
    while (!server.is_running()) {
      if (done.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready)
        throw std::runtime_error("application listener did not start");
      if (shutdown.requested(std::chrono::milliseconds(0))) {
        server.stop();
        listener.join();
        return done.get() ? 0 : 1;
      }
    }
    const auto url = "http://127.0.0.1:"+std::to_string(bound_port);
    output.ready(Json{{"url",url},{"environment",root.u8string()}}.dump());
#ifdef _WIN32
    if (opts.browser && !server_platform::open_browser(url))
      std::cerr << "The application is running. Open " << url << " in your browser.\n";
#endif
    while (done.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      if (shutdown.requested(std::chrono::milliseconds(100))) { server.stop(); break; }
    }
    listener.join();
    return done.get() ? 0 : 1;
  } catch (const std::exception&) {
    std::cerr << "Exchange server could not start or complete. Check its configuration, records and port.\n";
    return 1;
  }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  try {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i=0; i<argc; ++i) arguments.push_back(server_platform::utf8(argv[i]));
    return run(arguments);
  } catch (const std::exception&) {
    std::cerr << "Exchange could not read its Windows command arguments.\n";
    return 1;
  }
}
#else
int main(int argc, char** argv) {
  return run(std::vector<std::string>(argv,argv+argc));
}
#endif
