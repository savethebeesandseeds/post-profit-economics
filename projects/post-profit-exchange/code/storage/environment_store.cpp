// SPDX-License-Identifier: MIT
#include "storage/environment_store.hpp"

#include <db.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ph::exchange::storage {
namespace {
namespace fs = std::filesystem;

void validate_path(const fs::path& path) {
  const auto name = path.string();
  if (name.empty() || name.find('\0') != std::string::npos ||
      path.filename().empty() || path.filename() == "." || path.filename() == "..")
    throw std::runtime_error("environment database path must name a file and contain no NUL");
}

void validate_payload(const std::string& payload) {
  if (payload.empty() || payload.size() > environment_payload_limit)
    throw std::runtime_error("environment database payload must contain 1 to " +
                             std::to_string(environment_payload_limit) + " bytes");
  if (payload.find('\0') != std::string::npos)
    throw std::runtime_error("environment database payload must not contain embedded NUL");
}

[[noreturn]] void io_error(const std::string& message) {
  throw std::system_error(errno, std::generic_category(), message);
}

class Descriptor {
 public:
  explicit Descriptor(int value) : value_(value) {}
  ~Descriptor() { if (value_ >= 0) ::close(value_); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const { return value_; }
 private:
  int value_;
};

// Readers pin a file inode; writers keep the directory pinned through IdyDB's
// atomic inode replacement. Neither operation reopens a checked host path.
class Location {
 public:
  explicit Location(const fs::path& path)
      : name(path.filename().string()),
        directory(::open((path.has_parent_path() ? path.parent_path() : fs::path(".")).c_str(),
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) {
    if (directory.get() < 0) io_error("cannot open environment database directory");
  }
  std::string name;
  Descriptor directory;
};

void regular_file(int descriptor, bool bounded_database) {
  struct stat metadata {};
  if (::fstat(descriptor, &metadata) != 0) io_error("cannot inspect environment file");
  if (!S_ISREG(metadata.st_mode))
    throw std::runtime_error("environment database and lock must be regular files");
  if (bounded_database && metadata.st_size > 128 * 1024)
    throw std::runtime_error("environment database exceeds the file-size bound");
}

class WriterLock {
 public:
  explicit WriterLock(const Location& location)
      : descriptor_(::openat(location.directory.get(), (location.name + ".lock").c_str(),
                             O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600)) {
    if (descriptor_.get() < 0) io_error("cannot open environment writer lock");
    regular_file(descriptor_.get(), false);
    // This stable sibling is never renamed, truncated or removed by the adapter.
    while (::flock(descriptor_.get(), LOCK_EX) != 0) {
      if (errno != EINTR) io_error("cannot acquire environment writer lock");
    }
  }
 private:
  Descriptor descriptor_;
};

class Database {
 public:
  Database() = default;
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  ~Database() {
    if (handle_ && idydb_close(&handle_) != IDYDB_DONE) idydb_discard(&handle_);
  }

  void open(int descriptor, int flags) {
    const auto path = "/proc/self/fd/" + std::to_string(descriptor);
    expect(idydb_open(path.c_str(), &handle_, flags), IDYDB_SUCCESS, "open");
  }
  void open_at(int directory, const std::string& name, int reserved_descriptor) {
    struct stat reserved {}, current {};
    if (::fstat(reserved_descriptor, &reserved) != 0 ||
        ::fstatat(directory, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0)
      io_error("cannot inspect reserved environment database");
    if (!S_ISREG(reserved.st_mode) || !S_ISREG(current.st_mode) ||
        reserved.st_dev != current.st_dev || reserved.st_ino != current.st_ino)
      throw std::runtime_error("reserved environment database identity changed");
    expect(idydb_open_at(directory, name.c_str(), &handle_, 0), IDYDB_SUCCESS, "open reserved file");
  }
  idydb** handle() { return &handle_; }
  void expect(int status, int expected, const char* operation) {
    if (status == expected) return;
    const char* detail = idydb_errmsg(&handle_);
    throw std::runtime_error(std::string("environment database ") + operation +
                             " failed (IdyDB status " + std::to_string(status) +
                             "): " + (detail ? detail : "no detail"));
  }
  void close() {
    const int status = idydb_close(&handle_);
    if (status != IDYDB_DONE)
      throw std::runtime_error("environment database close failed (IdyDB status " +
                               std::to_string(status) + ")");
  }
 private:
  idydb* handle_ = nullptr;
};

std::string read_descriptor(int descriptor) {
  regular_file(descriptor, true);
  Database database;
  database.open(descriptor, IDYDB_READONLY);
  database.expect(idydb_extract(database.handle(), 1, 1), IDYDB_DONE, "extract envelope");
  if (idydb_retrieved_type(database.handle()) != IDYDB_CHAR)
    throw std::runtime_error("environment database envelope must be an IdyDB text cell");
  const char* text = idydb_retrieve_char(database.handle());
  if (!text)
    throw std::runtime_error("environment database envelope text is unavailable");
  const auto length = ::strnlen(text, environment_payload_limit + 1);
  if (length == 0 || length > environment_payload_limit)
    throw std::runtime_error("environment database envelope exceeds the payload bound");
  const std::string payload(text, length);
  database.close();
  return payload;
}

void sync_descriptor(int descriptor, const std::string& message) {
  while (::fsync(descriptor) != 0) {
    if (errno != EINTR) io_error(message);
  }
}

void write_new_descriptor(const Location& location, const std::string& name,
                          int descriptor, const std::string& payload) {
  Database database;
  database.open_at(location.directory.get(), name, descriptor);
  database.expect(idydb_insert_const_char(database.handle(), 1, 1, payload.c_str()),
                  IDYDB_DONE, "insert envelope");
  database.close();
  // IdyDB replaces the reserved inode. Reopen its persisted replacement through
  // the pinned directory, leaving the original reservation descriptor untouched.
  const Descriptor persisted(::openat(location.directory.get(), name.c_str(),
      O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  if (persisted.get() < 0) io_error("cannot reopen persisted environment database");
  if (read_descriptor(persisted.get()) != payload)
    throw std::runtime_error("environment database close/reopen verification failed");
  sync_descriptor(persisted.get(), "cannot synchronize environment database");
}

int open_existing(const Location& location) {
  const int descriptor = ::openat(location.directory.get(), location.name.c_str(),
                                  O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (descriptor < 0) io_error("cannot open existing environment database");
  return descriptor;
}

}  // namespace

void create_environment_store(const fs::path& database_path,
                              const std::string& payload) {
  validate_path(database_path);
  validate_payload(payload);
  const Location location(database_path);
  const WriterLock lock(location);
  // CREATE in IdyDB alone also opens existing files. Reserve exclusively first.
  const Descriptor descriptor(::openat(location.directory.get(), location.name.c_str(),
      O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (descriptor.get() < 0) io_error("cannot create new environment database");
  write_new_descriptor(location, location.name, descriptor.get(), payload);
  sync_descriptor(location.directory.get(), "cannot synchronize environment database directory");
}

std::string read_environment_store(const fs::path& database_path) {
  validate_path(database_path);
  const Location location(database_path);
  const Descriptor descriptor(open_existing(location));
  // Atomic replacement and this pinned inode let readers see a complete old or
  // new envelope without blocking a writer or opening a partial pending file.
  return read_descriptor(descriptor.get());
}

void replace_environment_store(const fs::path& database_path,
                               const std::string& expected,
                               const std::string& replacement) {
  validate_path(database_path);
  validate_payload(expected);
  validate_payload(replacement);
  const Location location(database_path);
  const WriterLock lock(location);
  const Descriptor original(open_existing(location));
  if (read_descriptor(original.get()) != expected)
    throw StoreConflict("environment database changed; reload before recording again");

  // Never reuse or remove another pending file. A failed attempt remains named
  // for inspection; subsequent attempts use a distinct exclusively created file.
  static std::atomic<unsigned long long> sequence {0};
  std::string pending;
  int pending_descriptor = -1;
  for (unsigned int attempt = 0; attempt < 100; ++attempt) {
    pending = location.name + ".pending." + std::to_string(::getpid()) + "." +
              std::to_string(sequence.fetch_add(1));
    pending_descriptor = ::openat(location.directory.get(), pending.c_str(),
        O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (pending_descriptor >= 0) break;
    if (errno != EEXIST) io_error("cannot create pending environment database");
  }
  if (pending_descriptor < 0)
    throw std::runtime_error("cannot reserve a unique pending environment database");
  const Descriptor staged(pending_descriptor);
  try {
    write_new_descriptor(location, pending, staged.get(), replacement);
    if (::renameat(location.directory.get(), pending.c_str(),
                   location.directory.get(), location.name.c_str()) != 0)
      io_error("cannot publish replacement environment database");
  } catch (const std::exception& error) {
    throw std::runtime_error(std::string(error.what()) + "; pending database retained at " +
                             (database_path.parent_path() / pending).string());
  }
  // A sync failure after rename is an uncertain durability result, not proof of
  // rollback. Tell the caller to reopen and check its event ID before retrying.
  sync_descriptor(location.directory.get(),
      "environment replacement published but directory sync failed; reopen and verify before retry");
}

}  // namespace ph::exchange::storage
