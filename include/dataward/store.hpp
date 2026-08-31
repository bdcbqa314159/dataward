#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace dataward {

// Handle to one open database. Move-only. The backend behind it (SOCI) is an
// implementation detail and must never leak into this header.
class Store {
 public:
  // Opens (creating if absent) a SQLite database at `path`.
  static Store sqlite(const std::string& path);

  ~Store();
  Store(Store&&) noexcept;
  Store& operator=(Store&&) noexcept;
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // ponytail: raw-SQL escape hatches prove the round-trip until the typed
  // Describe-driven API lands (sessions 2-4); they become private seam helpers then.
  void exec(const std::string& sql);
  std::optional<std::int64_t> query_int64(const std::string& sql);
  std::optional<std::string> query_string(const std::string& sql);

 private:
  struct Impl;
  explicit Store(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace dataward
