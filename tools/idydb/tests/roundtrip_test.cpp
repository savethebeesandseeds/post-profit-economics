// SPDX-License-Identifier: MIT
// Disposable functional smoke checks. These do not establish crash safety,
// transactional guarantees, thread safety, or suitability for financial records.
#include <db.h>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;

void check(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto root = std::filesystem::temp_directory_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      path_ = root / ("ph-idydb-smoke-" + std::to_string(stamp) + "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(path_)) return;
    }
    throw std::runtime_error("cannot create a disposable IdyDB smoke directory");
  }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path& path() const { return path_; }
 private:
  std::filesystem::path path_;
};

class Database {
 public:
  Database() = default;
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  ~Database() {
    if (handle_ && idydb_close(&handle_) != IDYDB_DONE) idydb_discard(&handle_);
  }
  int open(const std::filesystem::path& path, int flags, const char* passphrase = nullptr) {
    if (handle_) throw std::runtime_error("test tried to reopen a live handler");
    const auto filename = path.string();
    return passphrase ? idydb_open_encrypted(filename.c_str(), &handle_, flags, passphrase)
                      : idydb_open(filename.c_str(), &handle_, flags);
  }
  idydb** handler() { return &handle_; }
  std::string error() { return handle_ ? idydb_errmsg(&handle_) : "no handler"; }
  void expect(int actual, int expected, const std::string& message) {
    check(actual == expected, message + " (status " + std::to_string(actual) + ": " + error() + ")");
  }
  void close() { check(idydb_close(&handle_) == IDYDB_DONE, "close reports success"); }
 private:
  idydb* handle_ = nullptr;
};

constexpr const char* kText = "synthetic text record for the IdyDB smoke check";
constexpr const char* kPassphrase = "public-test-fixture-passphrase";
constexpr std::array<float, 3> kVector{{-1.5f, 0.25f, 2.0f}};

void write_fixture(Database& database) {
  auto h = database.handler();
  database.expect(idydb_insert_int(h, 1, 1, -17), IDYDB_DONE, "insert integer");
  database.expect(idydb_insert_float(h, 1, 2, 3.25f), IDYDB_DONE, "insert float");
  database.expect(idydb_insert_bool(h, 1, 3, true), IDYDB_DONE, "insert true");
  database.expect(idydb_insert_bool(h, 1, 4, false), IDYDB_DONE, "insert false");
  database.expect(idydb_insert_const_char(h, 2, 1, kText), IDYDB_DONE, "insert text");
  database.expect(idydb_insert_vector(h, 3, 1, kVector.data(), kVector.size()), IDYDB_DONE, "insert vector");
}

void extract(Database& database, idydb_column_row_sizing column,
             idydb_column_row_sizing row, int type) {
  auto h = database.handler();
  database.expect(idydb_extract(h, column, row), IDYDB_DONE, "extract persisted cell");
  check(idydb_retrieved_type(h) == type, "persisted cell retains its type");
}

void verify_fixture(Database& database) {
  auto h = database.handler();
  extract(database, 1, 1, IDYDB_INTEGER);
  check(idydb_retrieve_int(h) == -17, "integer survives close and reopen");
  extract(database, 1, 2, IDYDB_FLOAT);
  check(std::abs(idydb_retrieve_float(h) - 3.25f) < 1e-6f, "float survives close and reopen");
  extract(database, 1, 3, IDYDB_BOOL);
  check(idydb_retrieve_bool(h), "true survives close and reopen");
  extract(database, 1, 4, IDYDB_BOOL);
  check(!idydb_retrieve_bool(h), "false survives close and reopen");
  extract(database, 2, 1, IDYDB_CHAR);
  const char* text = idydb_retrieve_char(h);
  check(text && std::string(text) == kText, "text survives close and reopen");
  extract(database, 3, 1, IDYDB_VECTOR);
  unsigned short dimensions = 0;
  const float* vector = idydb_retrieve_vector(h, &dimensions);
  check(vector && dimensions == kVector.size(), "vector dimensions survive close and reopen");
  for (std::size_t index = 0; index < kVector.size(); ++index)
    check(std::abs(vector[index] - kVector[index]) < 1e-6f, "vector component survives close and reopen");
}

std::vector<char> read_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  check(input.is_open(), "open backing file for smoke comparison");
  std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  check(!input.bad(), "read backing file for smoke comparison");
  return bytes;
}

void roundtrip(const std::filesystem::path& path, const char* passphrase) {
  Database writer;
  writer.expect(writer.open(path, IDYDB_CREATE, passphrase), IDYDB_SUCCESS, "create disposable database");
  write_fixture(writer);

  Database contender;
  contender.expect(contender.open(path, 0, passphrase), IDYDB_BUSY, "second writer is refused while writer is open");
  contender.close();
  writer.close();

  Database reader;
  reader.expect(reader.open(path, IDYDB_READONLY, passphrase), IDYDB_SUCCESS, "reopen readonly database");
  verify_fixture(reader);
  reader.expect(idydb_insert_int(reader.handler(), 1, 1, 999), IDYDB_READONLY, "readonly refuses insert");
  reader.expect(idydb_delete(reader.handler(), 2, 1), IDYDB_READONLY, "readonly refuses delete");
  verify_fixture(reader);
  reader.close();

  Database reopened;
  reopened.expect(reopened.open(path, 0, passphrase), IDYDB_SUCCESS, "reopen writable database after lock release");
  verify_fixture(reopened);
  reopened.close();
}

void wrong_passphrase_preserves_file(const std::filesystem::path& path) {
  const auto original = read_bytes(path);
  check(original.size() > 68 && std::string(original.data(), 8) == "IDYDBENC",
        "encrypted backing has the expected container header");
  Database refused;
  check(refused.open(path, 0, "different-public-test-passphrase") == IDYDB_ERROR,
        "wrong passphrase is refused");
  refused.close();
  check(read_bytes(path) == original, "failed authentication preserves existing backing bytes");

  Database reader;
  reader.expect(reader.open(path, IDYDB_READONLY, kPassphrase), IDYDB_SUCCESS,
                "correct passphrase still opens database after refusal");
  verify_fixture(reader);
  reader.close();
}
}  // namespace

int main() {
  try {
    TemporaryDirectory disposable;
    roundtrip(disposable.path() / "plaintext.db", nullptr);
    const auto encrypted = disposable.path() / "encrypted.db";
    roundtrip(encrypted, kPassphrase);
    wrong_passphrase_preserves_file(encrypted);
    std::cout << "IdyDB functional smoke: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "IdyDB functional smoke: " << error.what() << '\n';
    return 1;
  }
}
