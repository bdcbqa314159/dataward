#include "dataward/store.hpp"

#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>

#include <deque>
#include <stdexcept>

namespace dataward {

namespace {

// Bind storage must stay alive and at a stable address until execute();
// deques never relocate elements.
struct Binder {
  std::deque<std::int64_t> ints;
  std::deque<double> doubles;
  std::deque<std::string> strings;
  std::deque<soci::indicator> inds;

  void bind(soci::statement& st, const Value& v) {
    switch (v.index()) {
      case 0: {  // SQL NULL — the bound type is irrelevant, the indicator rules
        auto& s = strings.emplace_back();
        st.exchange(soci::use(s, inds.emplace_back(soci::i_null)));
        break;
      }
      case 1: {
        auto& x = ints.emplace_back(std::get<std::int64_t>(v));
        st.exchange(soci::use(x, inds.emplace_back(soci::i_ok)));
        break;
      }
      case 2: {
        auto& x = doubles.emplace_back(std::get<double>(v));
        st.exchange(soci::use(x, inds.emplace_back(soci::i_ok)));
        break;
      }
      case 3: {
        auto& x = strings.emplace_back(std::get<std::string>(v));
        st.exchange(soci::use(x, inds.emplace_back(soci::i_ok)));
        break;
      }
    }
  }
};

Value cell_to_value(const soci::row& r, std::size_t i) {
  if (r.get_indicator(i) == soci::i_null) return std::monostate{};
  switch (r.get_properties(i).get_data_type()) {
    case soci::dt_string:
    case soci::dt_xml:
    case soci::dt_blob:
      return r.get<std::string>(i);
    case soci::dt_integer:
      return static_cast<std::int64_t>(r.get<int>(i));
    case soci::dt_long_long:
      return static_cast<std::int64_t>(r.get<long long>(i));
    case soci::dt_unsigned_long_long:
      return static_cast<std::int64_t>(r.get<unsigned long long>(i));
    case soci::dt_double:
      return r.get<double>(i);
    default:
      throw std::runtime_error("dataward: unsupported column type in result");
  }
}

}  // namespace

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

void Store::exec_bound(const std::string& sql, const std::vector<Value>& binds) {
  soci::statement st(impl_->sql);
  Binder b;
  for (const auto& v : binds) b.bind(st, v);
  st.alloc();
  st.prepare(sql);
  st.define_and_bind();
  st.execute(true);
}

std::vector<std::vector<Value>> Store::query_rows(const std::string& sql,
                                                  const std::vector<Value>& binds) {
  soci::row r;
  soci::statement st(impl_->sql);
  Binder b;
  for (const auto& v : binds) b.bind(st, v);
  st.exchange(soci::into(r));
  st.alloc();
  st.prepare(sql);
  st.define_and_bind();
  std::vector<std::vector<Value>> out;
  for (bool got = st.execute(true); got; got = st.fetch()) {
    std::vector<Value> row;
    row.reserve(r.size());
    for (std::size_t i = 0; i < r.size(); ++i) row.push_back(cell_to_value(r, i));
    out.push_back(std::move(row));
  }
  return out;
}

std::optional<std::vector<Value>> Store::query_row(const std::string& sql,
                                                   const std::vector<Value>& binds) {
  soci::row r;
  soci::statement st(impl_->sql);
  Binder b;
  for (const auto& v : binds) b.bind(st, v);
  st.exchange(soci::into(r));
  st.alloc();
  st.prepare(sql);
  st.define_and_bind();
  if (!st.execute(true)) return std::nullopt;
  std::vector<Value> out;
  out.reserve(r.size());
  for (std::size_t i = 0; i < r.size(); ++i) out.push_back(cell_to_value(r, i));
  return out;
}

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
