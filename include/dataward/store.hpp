#pragma once

#include <boost/describe/class.hpp>
#include <boost/describe/members.hpp>
#include <boost/mp11/algorithm.hpp>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "dataward/value.hpp"

namespace dataward {

namespace detail {

template <class T>
constexpr std::string_view wrapped_name() {
#if defined(_MSC_VER) && !defined(__clang__)
  return __FUNCSIG__;
#else
  return __PRETTY_FUNCTION__;
#endif
}

// Locate where the type spells itself inside the compiler-specific signature
// by probing with a type whose spelling is known.
inline constexpr std::string_view name_probe = wrapped_name<double>();
inline constexpr std::size_t name_prefix = name_probe.find("double");
inline constexpr std::size_t name_suffix = name_probe.size() - name_prefix - 6;

// Unqualified struct name — the table name. Derived from the compiler
// signature so the app declares nothing beyond BOOST_DESCRIBE_STRUCT.
template <class T>
constexpr std::string_view type_name() {
  std::string_view s = wrapped_name<T>();
  s.remove_prefix(name_prefix);
  s.remove_suffix(name_suffix);
  for (std::string_view kw : {"struct ", "class "})
    if (s.starts_with(kw)) s.remove_prefix(kw.size());
  if (auto pos = s.rfind("::"); pos != std::string_view::npos) s.remove_prefix(pos + 2);
  return s;
}

template <class T>
using members = boost::describe::describe_members<T, boost::describe::mod_public>;

template <class T, class D>
using member_type = std::remove_cvref_t<decltype(std::declval<T&>().*(D::pointer))>;

// Type map: C++ member type -> SQL column type + Value conversions. Unmapped
// types fail to compile here. optional<T> and year_month_day land in session 3.
template <class M>
struct column_traits;

template <std::integral M>
  requires(!std::same_as<M, bool>)
struct column_traits<M> {
  static constexpr const char* sql_type = "INTEGER";
  static Value to_value(M v) { return static_cast<std::int64_t>(v); }
  static M from_value(const Value& v) { return static_cast<M>(std::get<std::int64_t>(v)); }
};

template <>
struct column_traits<bool> {
  static constexpr const char* sql_type = "INTEGER";
  static Value to_value(bool v) { return static_cast<std::int64_t>(v); }
  static bool from_value(const Value& v) { return std::get<std::int64_t>(v) != 0; }
};

template <std::floating_point M>
struct column_traits<M> {
  static constexpr const char* sql_type = "REAL";
  static Value to_value(M v) { return static_cast<double>(v); }
  static M from_value(const Value& v) {
    // SQLite may hand a whole REAL back with integer affinity.
    if (std::holds_alternative<std::int64_t>(v)) return static_cast<M>(std::get<std::int64_t>(v));
    return static_cast<M>(std::get<double>(v));
  }
};

template <>
struct column_traits<std::string> {
  static constexpr const char* sql_type = "TEXT";
  static Value to_value(std::string v) { return v; }
  static std::string from_value(const Value& v) { return std::get<std::string>(v); }
};

// Dates as ISO "YYYY-MM-DD" TEXT — sorts correctly, readable in any DB shell.
template <>
struct column_traits<std::chrono::year_month_day> {
  static constexpr const char* sql_type = "TEXT";
  static Value to_value(std::chrono::year_month_day v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(v.year()),
                  static_cast<unsigned>(v.month()), static_cast<unsigned>(v.day()));
    return std::string(buf);
  }
  static std::chrono::year_month_day from_value(const Value& v) {
    const auto& s = std::get<std::string>(v);
    if (s.size() != 10 || s[4] != '-' || s[7] != '-')
      throw std::runtime_error("dataward: bad date '" + s + "', want YYYY-MM-DD");
    return std::chrono::year_month_day{std::chrono::year(std::stoi(s.substr(0, 4))),
                                       std::chrono::month(std::stoul(s.substr(5, 2))),
                                       std::chrono::day(std::stoul(s.substr(8, 2)))};
  }
};

template <class M>
struct is_optional : std::false_type {};
template <class U>
struct is_optional<std::optional<U>> : std::true_type {};

// optional<U> = U's column, nullable. NULL round-trips as nullopt.
template <class U>
struct column_traits<std::optional<U>> {
  static constexpr const char* sql_type = column_traits<U>::sql_type;
  static Value to_value(const std::optional<U>& v) {
    return v ? column_traits<U>::to_value(*v) : Value{std::monostate{}};
  }
  static std::optional<U> from_value(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return std::nullopt;
    return column_traits<U>::from_value(v);
  }
};

}  // namespace detail

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

  // Creates T's table if absent. Table = unqualified struct name; columns =
  // described members in declaration order; first member is the PRIMARY KEY.
  template <class T>
  void ensure() {
    using First = boost::mp11::mp_first<detail::members<T>>;
    static_assert(!detail::is_optional<detail::member_type<T, First>>::value,
                  "dataward: the first member is the primary key and cannot be optional");
    std::string sql = "CREATE TABLE IF NOT EXISTS \"";
    sql += detail::type_name<T>();
    sql += "\" (";
    bool first = true;
    boost::mp11::mp_for_each<detail::members<T>>([&](auto d) {
      using M = detail::member_type<T, decltype(d)>;
      if (!first) sql += ", ";
      sql += '"';
      sql += d.name;
      sql += "\" ";
      sql += detail::column_traits<M>::sql_type;
      if (first)
        sql += " PRIMARY KEY";
      else if (!detail::is_optional<M>::value)
        sql += " NOT NULL";
      first = false;
    });
    sql += ")";
    exec(sql);
  }

  // Upsert by primary key.
  template <class T>
  void put(const T& obj) {
    // ponytail: INSERT OR REPLACE is SQLite dialect; the dialect table arrives
    // with the MySQL backend (phase 3).
    std::string cols, marks;
    std::vector<Value> binds;
    std::size_t i = 0;
    boost::mp11::mp_for_each<detail::members<T>>([&](auto d) {
      using M = detail::member_type<T, decltype(d)>;
      if (i > 0) {
        cols += ", ";
        marks += ", ";
      }
      cols += '"';
      cols += d.name;
      cols += '"';
      marks += ":b" + std::to_string(i++);
      binds.push_back(detail::column_traits<M>::to_value(obj.*d.pointer));
    });
    std::string sql = "INSERT OR REPLACE INTO \"";
    sql += detail::type_name<T>();
    sql += "\" (" + cols + ") VALUES (" + marks + ")";
    exec_bound(sql, binds);
  }

  // Fetch by primary key; nullopt when absent.
  template <class T, class Pk>
  std::optional<T> get(const Pk& pk) {
    using First = boost::mp11::mp_first<detail::members<T>>;
    using M0 = detail::member_type<T, First>;

    std::string cols;
    boost::mp11::mp_for_each<detail::members<T>>([&](auto d) {
      if (!cols.empty()) cols += ", ";
      cols += '"';
      cols += d.name;
      cols += '"';
    });
    std::string sql = "SELECT " + cols + " FROM \"";
    sql += detail::type_name<T>();
    sql += "\" WHERE \"";
    sql += First::name;
    sql += "\" = :b0 LIMIT 1";

    auto row = query_row(sql, {detail::column_traits<M0>::to_value(M0(pk))});
    if (!row) return std::nullopt;
    T out{};
    std::size_t i = 0;
    boost::mp11::mp_for_each<detail::members<T>>([&](auto d) {
      using M = detail::member_type<T, decltype(d)>;
      out.*d.pointer = detail::column_traits<M>::from_value((*row)[i++]);
    });
    return out;
  }

  // ponytail: raw-SQL escape hatches prove the round-trip until the typed
  // Describe-driven API lands (sessions 2-4); they become private seam helpers then.
  void exec(const std::string& sql);
  std::optional<std::int64_t> query_int64(const std::string& sql);
  std::optional<std::string> query_string(const std::string& sql);

  // Seam: dynamic SQL with positional :bN binds — used by the typed templates.
  void exec_bound(const std::string& sql, const std::vector<Value>& binds);
  std::optional<std::vector<Value>> query_row(const std::string& sql,
                                              const std::vector<Value>& binds);

 private:
  struct Impl;
  explicit Store(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace dataward
