#include "dataward/store.hpp"

#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>
#ifdef DATAWARD_HAS_MYSQL
#include <soci/mysql/soci-mysql.h>
#endif
#ifdef DATAWARD_HAS_KEYWARD
#include <keyward/default_store.hpp>
#endif

#include <cstdlib>
#include <deque>
#include <utility>

namespace dataward {

namespace {

// Every SOCI failure crossing the seam becomes a typed dataward error.
template <class E, class F>
decltype(auto) rethrow_as(F&& f) {
  try {
    return std::forward<F>(f)();
  } catch (const soci::soci_error& e) {
    throw E(std::string("dataward: ") + e.what());
  }
}

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
      throw DecodeError("dataward: unsupported column type in result");
  }
}

}  // namespace

struct Store::Impl {
  soci::session sql;
  Dialect dialect = Dialect::sqlite;
};

Store::Store(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Store::~Store() = default;
Store::Store(Store&&) noexcept = default;
Store& Store::operator=(Store&&) noexcept = default;

Store Store::sqlite(const std::string& path) {
  return rethrow_as<OpenError>([&] {
    auto impl = std::make_unique<Impl>();
    impl->sql.open(*soci::factory_sqlite3(), path);
    return Store(std::move(impl));
  });
}

Store Store::mysql([[maybe_unused]] const std::string& connect) {
#ifdef DATAWARD_HAS_MYSQL
  return rethrow_as<OpenError>([&] {
    auto impl = std::make_unique<Impl>();
    impl->sql.open(*soci::factory_mysql(), connect);
    impl->dialect = Dialect::mysql;
    // dataward quotes identifiers with double quotes everywhere; MySQL only
    // accepts that under ANSI_QUOTES.
    impl->sql << "SET SESSION sql_mode = CONCAT(@@sql_mode, ',ANSI_QUOTES')";
    return Store(std::move(impl));
  });
#else
  throw OpenError("dataward: built without MySQL support (DATAWARD_WITH_MYSQL=OFF)");
#endif
}

Dialect Store::dialect() const { return impl_->dialect; }

namespace {

// keyward://app/name -> defaultSecretStore(app)->get(name)
std::optional<std::string> resolve_keyward([[maybe_unused]] const std::string& app,
                                           [[maybe_unused]] const std::string& name) {
#ifdef DATAWARD_HAS_KEYWARD
  return keyward::defaultSecretStore(app)->get(name);
#else
  throw OpenError("dataward: keyward:// secrets need DATAWARD_WITH_KEYWARD=ON");
#endif
}

// The fail-loudly chain: a configured source that yields nothing throws.
std::string resolve_password(const Profile& p) {
  if (!p.password_env.empty()) {
    if (const char* v = std::getenv(p.password_env.c_str()); v != nullptr && *v != '\0') return v;
    if (p.password_secret.empty())
      throw OpenError("dataward: password env var '" + p.password_env + "' is unset or empty");
  }
  if (!p.password_secret.empty()) {
    constexpr std::string_view scheme = "keyward://";
    if (p.password_secret.rfind(scheme, 0) != 0)
      throw OpenError("dataward: unsupported secret scheme in '" + p.password_secret +
                      "' (only keyward://app/name)");
    const std::string rest = p.password_secret.substr(scheme.size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 == rest.size())
      throw OpenError("dataward: malformed secret uri '" + p.password_secret +
                      "', want keyward://app/name");
    auto secret = resolve_keyward(rest.substr(0, slash), rest.substr(slash + 1));
    if (secret.has_value() && !secret->empty()) return *secret;
    throw OpenError("dataward: secret '" + p.password_secret + "' not found or empty");
  }
  return "";
}

}  // namespace

Store open(const Profile& p) {
  if (p.backend == Dialect::sqlite) {
    if (p.path.empty()) throw OpenError("dataward: sqlite profile needs a path");
    return Store::sqlite(p.path);
  }
  std::string connect =
      "db=" + p.db + " user=" + p.user + " host=" + p.host + " port=" + std::to_string(p.port);
  const std::string password = resolve_password(p);
  if (!password.empty()) {
    if (password.find('\'') != std::string::npos)
      throw OpenError("dataward: passwords containing ' are not supported in connect strings");
    connect += " password='" + password + "'";
  }
  return Store::mysql(connect);
}

void Store::exec(const std::string& sql) {
  rethrow_as<QueryError>([&] { impl_->sql << sql; });
}

void Store::exec_bound(const std::string& sql, const std::vector<Value>& binds) {
  rethrow_as<QueryError>([&] {
    soci::statement st(impl_->sql);
    Binder b;
    for (const auto& v : binds) b.bind(st, v);
    st.alloc();
    st.prepare(sql);
    st.define_and_bind();
    st.execute(true);
  });
}

std::vector<std::vector<Value>> Store::query_rows(const std::string& sql,
                                                  const std::vector<Value>& binds) {
  return rethrow_as<QueryError>([&] {
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
  });
}

std::optional<std::vector<Value>> Store::query_row(const std::string& sql,
                                                   const std::vector<Value>& binds) {
  return rethrow_as<QueryError>([&]() -> std::optional<std::vector<Value>> {
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
  });
}

std::optional<std::int64_t> Store::query_int64(const std::string& sql) {
  return rethrow_as<QueryError>([&]() -> std::optional<std::int64_t> {
    std::int64_t value = 0;
    soci::indicator ind = soci::i_null;
    impl_->sql << sql, soci::into(value, ind);
    if (!impl_->sql.got_data() || ind != soci::i_ok) return std::nullopt;
    return value;
  });
}

std::optional<std::string> Store::query_string(const std::string& sql) {
  return rethrow_as<QueryError>([&]() -> std::optional<std::string> {
    std::string value;
    soci::indicator ind = soci::i_null;
    impl_->sql << sql, soci::into(value, ind);
    if (!impl_->sql.got_data() || ind != soci::i_ok) return std::nullopt;
    return value;
  });
}

}  // namespace dataward
