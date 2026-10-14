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
void create_environment_store(const std::filesystem::path& database_path,
                              const std::string& payload);

// Opens only an existing regular file, reads cell (1,1) as text, then closes.
// Does not create missing paths or replace an invalid database.
std::string read_environment_store(const std::filesystem::path& database_path);

// A definite stale revision rejection, distinct from uncertain I/O outcomes.
class StoreConflict : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Compare the exact persisted envelope under the writer lock. A stale expected
// value fails without changing it. The verified replacement is flushed in a new
// sibling IdyDB file, atomically renamed, then the directory is synchronized.
// Failed pre-publication attempts retain the pending file for inspection.
// A failure synchronizing the directory after publication has an uncertain
// durability outcome: reopen and check the recorded event before any retry.
void replace_environment_store(const std::filesystem::path& database_path,
                               const std::string& expected,
                               const std::string& replacement);

}  // namespace ph::exchange::storage
