// SPDX-License-Identifier: MIT
#include "storage/environment_store.hpp"

#include <db.h>

#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using ph::exchange::storage::create_environment_store;
using ph::exchange::storage::environment_payload_limit;
using ph::exchange::storage::read_environment_store;
using ph::exchange::storage::replace_environment_store;
using ph::exchange::storage::StoreConflict;
int checks = 0;

void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

template <class Function>
void rejects(Function action, const std::string& message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  require(rejected, message);
}

std::string bytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open test file");
  const std::string result{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  if (input.bad()) throw std::runtime_error("cannot read test file");
  return result;
}

void direct_idydb_type_fixture(const fs::path& path) {
  idydb* handle = nullptr;
  try {
    require(idydb_open(path.string().c_str(), &handle, IDYDB_CREATE) == IDYDB_SUCCESS,
            "create wrong-type test database");
    require(idydb_insert_int(&handle, 1, 1, 17) == IDYDB_DONE,
            "write wrong-type database cell");
    require(idydb_close(&handle) == IDYDB_DONE, "close wrong-type test database");
  } catch (...) {
    if (handle) idydb_close(&handle);
    throw;
  }
}

void directly_verify_text(const fs::path& path, const std::string& expected) {
  idydb* handle = nullptr;
  try {
    require(idydb_open(path.string().c_str(), &handle, IDYDB_READONLY) == IDYDB_SUCCESS,
            "open adapter output directly with IdyDB");
    require(idydb_extract(&handle, 1, 1) == IDYDB_DONE,
            "envelope is in IdyDB cell (1,1)");
    require(idydb_retrieved_type(&handle) == IDYDB_CHAR, "envelope is a text cell");
    const auto* text = idydb_retrieve_char(&handle);
    require(text && expected == text, "direct IdyDB read retains exact payload");
    require(idydb_close(&handle) == IDYDB_DONE, "close direct verification reader");
  } catch (...) {
    if (handle) idydb_close(&handle);
    throw;
  }
}

void pinned_reader_survives_replace(const fs::path& path, const std::string& expected,
                                   const std::string& replacement) {
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  require(descriptor >= 0, "pin original database before replacement");
  try {
    struct stat original {}, current {};
    require(::fstat(descriptor, &original) == 0, "inspect pinned original database");
    replace_environment_store(path, expected, replacement);
    require(::stat(path.c_str(), &current) == 0, "inspect published replacement database");
    require(original.st_dev != current.st_dev || original.st_ino != current.st_ino,
            "replacement publishes a new inode");
    directly_verify_text("/proc/self/fd/" + std::to_string(descriptor), expected);
    require(read_environment_store(path) == replacement,
            "new readers see replacement while pinned reader retains original");
  } catch (...) {
    ::close(descriptor);
    throw;
  }
  ::close(descriptor);
}
}  // namespace

int main() {
  fs::path directory;
  try {
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    directory = fs::temp_directory_path() / ("exchange-environment-store-" +
                                             std::to_string(token));
    if (!fs::create_directory(directory))
      throw std::runtime_error("cannot create unique test directory");

    // Numeric tokens deliberately exceed float and JavaScript exact integer
    // ranges. Text transport must retain their spelling without conversion.
    const std::string payload = R"json({
  "schema_version": "exchange.environment.v1",
  "cash_minor": 9223372036854775807,
  "minimum": -9223372036854775808,
  "decimal": 1234567890.12345678901234567890,
  "label": "Sample café: \"quoted\" and \\ slash",
  "escaped": "line\nnext\tcolumn\u0000"
})json";
    const auto store = directory / "operation.idydb";
    create_environment_store(store, payload);
    require(fs::is_regular_file(store), "creation writes a database file");
    directly_verify_text(store, payload);
    const auto original_bytes = bytes(store);
    require(read_environment_store(store) == payload, "reopen retains exact JSON bytes");
    require(read_environment_store(store) == payload, "successive reopen retains exact bytes");
    require(bytes(store) == original_bytes, "readonly opens preserve database bytes");
    require(static_cast<std::size_t>(std::distance(fs::directory_iterator(directory),
                                                 fs::directory_iterator())) == 2,
            "database has only its stable writer lock, no JSON sidecar");
    require(fs::is_regular_file(store.string() + ".lock"), "stable writer lock is present");

    rejects([&] { create_environment_store(store, "{\"replacement\":true}"); },
            "repeat initialization is refused");
    require(bytes(store) == original_bytes, "repeat initialization preserves existing bytes");
    require(read_environment_store(store) == payload, "repeat initialization preserves data");

    const auto missing = directory / "missing.idydb";
    rejects([&] { read_environment_store(missing); }, "missing database read fails");
    require(!fs::exists(missing), "missing database read creates no file");
    const auto missing_parent = directory / "missing-parent" / "store.idydb";
    rejects([&] { read_environment_store(missing_parent); }, "missing parent read fails");
    rejects([&] { create_environment_store(missing_parent, payload); },
            "adapter does not silently create environment directories");
    require(!fs::exists(missing_parent.parent_path()), "missing parent remains absent");

    for (const auto& invalid : {std::string(), std::string(environment_payload_limit + 1, 'x'),
                               std::string("one\0two", 7)}) {
      rejects([&] { create_environment_store(missing, invalid); },
              "invalid transport payload is rejected");
      require(!fs::exists(missing), "invalid payload is rejected before creating database");
    }

    const auto folder = directory / "not-a-database";
    fs::create_directory(folder);
    rejects([&] { read_environment_store(folder); }, "directory cannot be read as database");
    rejects([&] { create_environment_store(folder, payload); }, "directory is never replaced");
    const auto link = directory / "linked.idydb";
    fs::create_symlink(store, link);
    rejects([&] { read_environment_store(link); }, "database symlink read is refused");
    rejects([&] { create_environment_store(link, payload); }, "database symlink is never replaced");
    require(bytes(store) == original_bytes, "symlink target remains unchanged");

    const auto wrong_type = directory / "wrong-type.idydb";
    direct_idydb_type_fixture(wrong_type);
    const auto wrong_before = bytes(wrong_type);
    rejects([&] { read_environment_store(wrong_type); }, "non-text envelope is rejected");
    require(bytes(wrong_type) == wrong_before, "failed type validation preserves file");

    const auto empty = directory / "empty.idydb";
    { std::ofstream output(empty, std::ios::binary); }
    rejects([&] { read_environment_store(empty); }, "empty database has no envelope");
    require(fs::is_regular_file(empty) && fs::file_size(empty) == 0,
            "failed read preserves empty database for inspection");
    rejects([&] { create_environment_store(empty, payload); },
            "existing incomplete database is not silently replaced");
    require(fs::file_size(empty) == 0, "refused repair preserves incomplete file");

    const auto largest = directory / "largest.idydb";
    const std::string prefix = "{\"text\":\"";
    const std::string suffix = "\"}";
    const auto at_limit = prefix +
        std::string(environment_payload_limit - prefix.size() - suffix.size(), 'x') + suffix;
    create_environment_store(largest, at_limit);
    require(read_environment_store(largest) == at_limit, "bounded maximum payload reopens exactly");


    // A pending file can belong to an interrupted attempt. Never truncate it or
    // silently promote it; only the published database is authoritative.
    const auto marker = fs::path(store.string() + ".pending." +
                                  std::to_string(::getpid()) + ".0");
    { std::ofstream output(marker); output << "preserve interrupted attempt"; }
    const std::string replacement = R"({"schema_version":"exchange.environment.v2","revision":1,"cash":10001})";
    pinned_reader_survives_replace(store, payload, replacement);
    require(read_environment_store(store) == replacement, "replacement reopens with exact text");
    directly_verify_text(store, replacement);
    require(bytes(store) != original_bytes, "successful replacement publishes new database bytes");
    require(bytes(marker) == "preserve interrupted attempt", "pending collision marker remains intact");
    const auto replaced_bytes = bytes(store);
    bool conflict = false;
    try { replace_environment_store(store, payload, "{\"stale\":true}"); }
    catch (const StoreConflict&) { conflict = true; }
    require(conflict, "stale compare reports the distinct StoreConflict type");
    require(bytes(store) == replaced_bytes, "stale compare preserves complete current database");
    for (const auto& invalid : {std::string(), std::string(environment_payload_limit + 1, 'x'),
                               std::string("one\0two", 7)}) {
      rejects([&] { replace_environment_store(store, replacement, invalid); },
              "invalid replacement payload is rejected");
      require(bytes(store) == replaced_bytes, "rejected payload preserves exact current bytes");
    }
    rejects([&] { replace_environment_store(missing, replacement, payload); },
            "replace never creates a missing database");
    require(!fs::exists(missing), "missing replacement leaves database absent");
    rejects([&] { replace_environment_store(link, replacement, payload); },
            "replace refuses a database symlink");
    require(bytes(store) == replaced_bytes, "rejected symlink replacement preserves target");
    const auto lock_link_store = directory / "lock-link.idydb";
    fs::copy_file(store, lock_link_store);
    fs::create_symlink(marker, lock_link_store.string() + ".lock");
    rejects([&] { replace_environment_store(lock_link_store, replacement, payload); },
            "writer refuses symlink lock");
    require(bytes(lock_link_store) == replaced_bytes, "rejected lock preserves database");
    require(bytes(marker) == "preserve interrupted attempt", "rejected lock preserves symlink target");

    // Two separate writers observe the same revision before beginning. The
    // stable lock must cover both compare and publication, so exactly one wins.
    int start_pipe[2];
    require(::pipe(start_pipe) == 0, "create competing writer start gate");
    pid_t writers[2];
    const std::string competing[] = {
      R"({"schema_version":"exchange.environment.v2","revision":2,"winner":"first"})",
      R"({"schema_version":"exchange.environment.v2","revision":2,"winner":"second"})"
    };
    for (int i = 0; i < 2; ++i) {
      writers[i] = ::fork();
      require(writers[i] >= 0, "fork competing writer");
      if (writers[i] == 0) {
        ::close(start_pipe[1]);
        char gate;
        if (::read(start_pipe[0], &gate, 1) != 1) ::_exit(20);
        ::close(start_pipe[0]);
        try {
          replace_environment_store(store, replacement, competing[i]);
          ::_exit(0);
        } catch (const std::exception&) {
          try {
            const auto current = read_environment_store(store);
            ::_exit(current == competing[1 - i] ? 3 : 21);
          } catch (...) { ::_exit(22); }
        }
      }
    }
    ::close(start_pipe[0]);
    require(::write(start_pipe[1], "go", 2) == 2, "release both competing writers");
    ::close(start_pipe[1]);
    int winners = 0, stale = 0;
    for (const auto writer : writers) {
      int status = 0;
      require(::waitpid(writer, &status, 0) == writer && WIFEXITED(status),
              "competing writer exits normally");
      winners += WEXITSTATUS(status) == 0;
      stale += WEXITSTATUS(status) == 3;
    }
    require(winners == 1 && stale == 1, "exactly one same-revision writer commits");
    const auto current = read_environment_store(store);
    require(current == competing[0] || current == competing[1], "reopen returns the winning envelope");

    // Force a partial pending write in a child only. The actual database must
    // stay unchanged, and evidence of the failed attempt must remain inspectable.
    const auto before_failure = bytes(store);
    const auto failed_writer = ::fork();
    require(failed_writer >= 0, "fork bounded failed writer");
    if (failed_writer == 0) {
      ::signal(SIGXFSZ, SIG_IGN);
      const struct rlimit limit {16, 16};
      if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) ::_exit(30);
      try {
        replace_environment_store(store, current, std::string(1000, 'x'));
        ::_exit(31);
      } catch (const std::exception& error) {
        ::_exit(std::string(error.what()).find("pending database retained") != std::string::npos
                    ? 0 : 32);
      }
    }
    int failed_status = 0;
    require(::waitpid(failed_writer, &failed_status, 0) == failed_writer &&
            WIFEXITED(failed_status) && WEXITSTATUS(failed_status) == 0,
            "partial pending write is reported with its retained path");
    require(bytes(store) == before_failure, "failed staging preserves original database bytes");
    require(read_environment_store(store) == current, "restart ignores unpublished partial files");
    bool retained_pending = false;
    const auto failed_prefix = store.filename().string() + ".pending." +
                               std::to_string(failed_writer) + ".";
    for (const auto& entry : fs::directory_iterator(directory))
      retained_pending |= entry.path().filename().string().find(failed_prefix) == 0;
    require(retained_pending, "partial pending file is retained for inspection");
    require(bytes(marker) == "preserve interrupted attempt", "unrelated pending marker is preserved");

    fs::remove_all(directory);
    std::cout << "Exchange environment storage: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Exchange environment storage: " << error.what()
              << "\nTest files preserved in " << directory << '\n';
    return 1;
  }
}
