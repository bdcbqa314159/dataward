#include "dataward/store.hpp"

#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>

namespace dataward {

struct Store::Impl {
  soci::session sql;
};

Store::Store(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Store::~Store() = default;
Store::Store(Store&&) noexcept = default;
Store& Store::operator=(Store&&) noexcept = default;

Store Store::sqlite(const std::string& path) {
  auto impl = std::make_unique<Impl>();
  impl->sql.open(*soci::factory_sqlite3(), path);
  return Store(std::move(impl));
}

void Store::exec(const std::string& sql) { impl_->sql << sql; }

std::optional<std::int64_t> Store::query_int64(const std::string& sql) {
  std::int64_t value = 0;
  soci::indicator ind = soci::i_null;
  impl_->sql << sql, soci::into(value, ind);
  if (!impl_->sql.got_data() || ind != soci::i_ok) return std::nullopt;
  return value;
}

std::optional<std::string> Store::query_string(const std::string& sql) {
  std::string value;
  soci::indicator ind = soci::i_null;
  impl_->sql << sql, soci::into(value, ind);
  if (!impl_->sql.got_data() || ind != soci::i_ok) return std::nullopt;
  return value;
}

}  // namespace dataward
