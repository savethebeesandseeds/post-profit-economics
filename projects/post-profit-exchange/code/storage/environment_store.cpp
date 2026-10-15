// SPDX-License-Identifier: MIT
#include "storage/environment_store.hpp"

#include <db.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <linux/magic.h>
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

void validate_passphrase(const std::string& passphrase) {
  if (passphrase.find('\0') != std::string::npos)
    throw std::runtime_error("environment database passphrase must not contain NUL");
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

// The encrypted IdyDB API takes a pathname, unlike its plaintext open_at API.
// Only ciphertext is copied into this private tmpfs directory. IdyDB manages
// decrypted working storage in memory. No plaintext sidecar is ever created.
class PrivateCiphertext {
 public:
  PrivateCiphertext()
      : root_(::open("/dev/shm", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) {
    if (root_.get() < 0) io_error("cannot open private ciphertext storage");
    struct statfs filesystem {};
    if (::fstatfs(root_.get(), &filesystem) != 0)
      io_error("cannot inspect private ciphertext storage");
    if (filesystem.f_type != TMPFS_MAGIC)
      throw std::runtime_error("private ciphertext storage requires /dev/shm on tmpfs");
    static std::atomic<unsigned long long> sequence {0};
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
      name_ = "exchange-ciphertext-" + std::to_string(::getpid()) + "." +
              std::to_string(sequence.fetch_add(1));
      if (::mkdirat(root_.get(), name_.c_str(), 0700) == 0) {
        created_ = true;
        break;
      }
      if (errno != EEXIST) io_error("cannot reserve private ciphertext directory");
    }
    if (!created_)
      throw std::runtime_error("cannot reserve unique private ciphertext directory");
    try {
      directory_ = ::openat(root_.get(), name_.c_str(),
                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if (directory_ < 0) io_error("cannot open private ciphertext directory");
      struct stat metadata {};
      if (::fstat(directory_, &metadata) != 0)
        io_error("cannot inspect private ciphertext directory");
      if (!S_ISDIR(metadata.st_mode) || metadata.st_uid != ::geteuid() ||
          (metadata.st_mode & 0777) != 0700)
        throw std::runtime_error("private ciphertext directory has unsafe permissions");
      const Descriptor image(::openat(directory_, "image.idydb",
          O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
      if (image.get() < 0) io_error("cannot reserve private ciphertext image");
      image_created_ = true;
    } catch (...) {
      cleanup();
      throw;
    }
  }
  ~PrivateCiphertext() { cleanup(); }
  PrivateCiphertext(const PrivateCiphertext&) = delete;
  PrivateCiphertext& operator=(const PrivateCiphertext&) = delete;
  std::string path() const {
    return "/proc/self/fd/" + std::to_string(directory_) + "/image.idydb";
  }
  Descriptor open_image(int flags) const {
    const int descriptor = ::openat(directory_, "image.idydb",
                                    flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) io_error("cannot open private ciphertext image");
    return Descriptor(descriptor);
  }
 private:
  void cleanup() noexcept {
    // Remove only this adapter's own file and empty directory, never recurse.
    bool same_directory = false;
    if (directory_ >= 0) {
      struct stat pinned {}, named {};
      same_directory = ::fstat(directory_, &pinned) == 0 &&
          ::fstatat(root_.get(), name_.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
          S_ISDIR(named.st_mode) && pinned.st_dev == named.st_dev &&
          pinned.st_ino == named.st_ino;
      if (image_created_) ::unlinkat(directory_, "image.idydb", 0);
      ::close(directory_);
      directory_ = -1;
    }
    if (created_ && same_directory)
      ::unlinkat(root_.get(), name_.c_str(), AT_REMOVEDIR);
    created_ = false;
  }
  Descriptor root_;
  std::string name_;
  int directory_ = -1;
  bool created_ = false;
  bool image_created_ = false;
};

void copy_ciphertext(int source, int destination) {
  regular_file(source, true);
  regular_file(destination, true);
  std::array<char, 8192> buffer {};
  off_t offset = 0;
  while (true) {
    const auto size = ::pread(source, buffer.data(), buffer.size(), offset);
    if (size < 0) {
      if (errno == EINTR) continue;
      io_error("cannot read environment ciphertext");
    }
    if (size == 0) break;
    if (offset + size > 128 * 1024)
      throw std::runtime_error("environment ciphertext exceeds the file-size bound");
    ssize_t written = 0;
    while (written < size) {
      const auto count = ::pwrite(destination, buffer.data() + written,
                                  static_cast<std::size_t>(size - written), offset + written);
      if (count < 0) {
        if (errno == EINTR) continue;
        io_error("cannot write environment ciphertext");
      }
      if (count == 0) throw std::runtime_error("environment ciphertext write made no progress");
      written += count;
    }
    offset += size;
  }
  if (::ftruncate(destination, offset) != 0)
    io_error("cannot finalize environment ciphertext");
}

void verify_reserved(int directory, const std::string& name, int descriptor) {
  struct stat reserved {}, current {};
  if (::fstat(descriptor, &reserved) != 0 ||
      ::fstatat(directory, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0)
    io_error("cannot inspect reserved environment database");
  if (!S_ISREG(reserved.st_mode) || !S_ISREG(current.st_mode) ||
      reserved.st_dev != current.st_dev || reserved.st_ino != current.st_ino)
    throw std::runtime_error("reserved environment database identity changed");
}

class Database {
 public:
  Database() = default;
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  ~Database() {
    // Successful writes close explicitly. Never retry a failed write on unwind.
    if (handle_) idydb_discard(&handle_);
  }

  void open(int descriptor, int flags) {
    const auto path = "/proc/self/fd/" + std::to_string(descriptor);
    expect(idydb_open(path.c_str(), &handle_, flags), IDYDB_SUCCESS, "open");
  }
  void open_at(int directory, const std::string& name, int reserved_descriptor) {
    verify_reserved(directory, name, reserved_descriptor);
    expect(idydb_open_at(directory, name.c_str(), &handle_, 0), IDYDB_SUCCESS, "open reserved file");
  }
  void open_encrypted(const std::string& path, int flags, const std::string& passphrase) {
    expect(idydb_open_encrypted(path.c_str(), &handle_, flags, passphrase.c_str()),
           IDYDB_SUCCESS, "open encrypted file");
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

std::string extract_payload(Database& database) {
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

std::string read_descriptor(int descriptor, const std::string& passphrase) {
  regular_file(descriptor, true);
  if (!passphrase.empty()) {
    std::array<char, 8> magic {};
    ssize_t size;
    do { size = ::pread(descriptor, magic.data(), magic.size(), 0); } while (size < 0 && errno == EINTR);
    if (size < 0) io_error("cannot inspect encrypted environment database");
    if (size != static_cast<ssize_t>(magic.size()) ||
        std::memcmp(magic.data(), "IDYDBENC", magic.size()) != 0)
      throw std::runtime_error("encrypted environment requires native IdyDB ciphertext; no plaintext fallback");
    PrivateCiphertext image;
    const auto target = image.open_image(O_RDWR);
    copy_ciphertext(descriptor, target.get());
    Database database;
    database.open_encrypted(image.path(), IDYDB_READONLY, passphrase);
    return extract_payload(database);
  }
  Database database;
  database.open(descriptor, IDYDB_READONLY);
  return extract_payload(database);
}

void sync_descriptor(int descriptor, const std::string& message) {
  while (::fsync(descriptor) != 0) {
    if (errno != EINTR) io_error(message);
  }
}

void write_new_descriptor(const Location& location, const std::string& name,
                          int descriptor, const std::string& payload,
                          const std::string& passphrase) {
  if (!passphrase.empty()) {
    verify_reserved(location.directory.get(), name, descriptor);
    PrivateCiphertext image;
    Database database;
    // Writable encrypted opens are limited to our own empty image. Existing
    // operation databases are always authenticated read-only first.
    database.open_encrypted(image.path(), IDYDB_CREATE, passphrase);
    database.expect(idydb_insert_const_char(database.handle(), 1, 1, payload.c_str()),
                    IDYDB_DONE, "insert encrypted envelope");
    database.close();
    const auto encrypted = image.open_image(O_RDONLY);
    if (read_descriptor(encrypted.get(), passphrase) != payload)
      throw std::runtime_error("environment ciphertext verification failed");
    copy_ciphertext(encrypted.get(), descriptor);
    sync_descriptor(descriptor, "cannot synchronize encrypted environment database");
    // Verify the reserved destination through the pinned directory before its
    // publication. Unlike plaintext IdyDB writes, this inode is not replaced.
    verify_reserved(location.directory.get(), name, descriptor);
    const Descriptor persisted(::openat(location.directory.get(), name.c_str(),
        O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    if (persisted.get() < 0) io_error("cannot reopen encrypted environment database");
    if (read_descriptor(persisted.get(), passphrase) != payload)
      throw std::runtime_error("encrypted environment close/reopen verification failed");
    return;
  }
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
  if (read_descriptor(persisted.get(), passphrase) != payload)
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
                              const std::string& payload,
                              const std::string& passphrase) {
  validate_path(database_path);
  validate_passphrase(passphrase);
  validate_payload(payload);
  const Location location(database_path);
  const WriterLock lock(location);
  // CREATE in IdyDB alone also opens existing files. Reserve exclusively first.
  const Descriptor descriptor(::openat(location.directory.get(), location.name.c_str(),
      O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (descriptor.get() < 0) io_error("cannot create new environment database");
  write_new_descriptor(location, location.name, descriptor.get(), payload, passphrase);
  sync_descriptor(location.directory.get(), "cannot synchronize environment database directory");
}

std::string read_environment_store(const fs::path& database_path,
                                   const std::string& passphrase) {
  validate_path(database_path);
  validate_passphrase(passphrase);
  const Location location(database_path);
  const Descriptor descriptor(open_existing(location));
  // Atomic replacement and this pinned inode let readers see a complete old or
  // new envelope without blocking a writer or opening a partial pending file.
  return read_descriptor(descriptor.get(), passphrase);
}

void replace_environment_store(const fs::path& database_path,
                               const std::string& expected,
                               const std::string& replacement,
                               const std::string& passphrase) {
  validate_path(database_path);
  validate_passphrase(passphrase);
  validate_payload(expected);
  validate_payload(replacement);
  const Location location(database_path);
  const WriterLock lock(location);
  const Descriptor original(open_existing(location));
  if (read_descriptor(original.get(), passphrase) != expected)
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
    write_new_descriptor(location, pending, staged.get(), replacement, passphrase);
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
