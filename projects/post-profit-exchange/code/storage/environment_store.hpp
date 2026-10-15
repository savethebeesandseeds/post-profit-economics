// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <stdexcept>

namespace ph::exchange::storage {

// One bounded, versioned environment envelope, encoded as text to retain exact
// 64-bit integers and decimal spellings. The environment layer owns JSON/schema
// validation; this adapter owns only persistence and transport validation.
// This stays below IdyDB's text-cell capacity including its length/terminator.
inline constexpr std::size_t environment_payload_limit = 60 * 1024;

// Create once. Requires an existing parent directory. Existing destinations are
// never overwritten. Any partial database remains on failure for inspection.
// Initialization and replacement share a persistent sibling ".lock" file.
// A nonempty passphrase selects IdyDB encryption. Linux retains legacy plaintext
// for an empty passphrase; Windows always requires encryption.
// Encrypted access never falls back to plaintext or implicitly migrates a file.
void create_environment_store(const std::filesystem::path& database_path,
                              const std::string& payload,
                              const std::string& passphrase = {});

// Opens only an existing regular file, reads cell (1,1) as text, then closes.
// Does not initialize missing databases or replace an invalid database. Windows
// may create its stable sibling lock when opening an existing encrypted file.
std::string read_environment_store(const std::filesystem::path& database_path,
                                   const std::string& passphrase = {});

// A definite stale revision rejection, distinct from uncertain I/O outcomes.
class StoreConflict : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Compare the exact persisted envelope under the writer lock. A stale expected
// value fails without changing it. Linux verifies a new sibling IdyDB image,
// atomically renames it, then synchronizes the directory; failed staging remains
// inspectable. Windows compares and writes through the same encrypted IdyDB
// handle, delegates atomic publication/durability to the supplied Windows library,
// and verifies by reopening. A post-publication/close/reopen failure can have an
// uncertain outcome: reopen and check the recorded event before any retry.
void replace_environment_store(const std::filesystem::path& database_path,
                               const std::string& expected,
                               const std::string& replacement,
                               const std::string& passphrase = {});

}  // namespace ph::exchange::storage
