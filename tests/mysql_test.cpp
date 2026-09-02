#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

#include "dataward/store.hpp"

// The full typed API against a real MySQL. Gated on DATAWARD_MYSQL_TEST
// holding a SOCI connect string ("db=... user=... password=... host=...");
// without it (or in a build without MySQL) the tests skip, so plain
// `ctest` needs no server.

namespace {

struct Span {
  std::string id;
  std::string label;
  std::int64_t n;
  double x;
  bool flag;
  std::chrono::year_month_day day;
  std::optional<std::string> note;
};
BOOST_DESCRIBE_STRUCT(Span, (), (id, label, n, x, flag, day, note))

std::optional<dataward::Store> mysql_store() {
  const char* connect = std::getenv("DATAWARD_MYSQL_TEST");
  if (connect == nullptr) return std::nullopt;
  auto store = dataward::Store::mysql(connect);
  store.exec("DROP TABLE IF EXISTS \"Span\"");
  return store;
}

#define REQUIRE_MYSQL(store)                                                       \
  auto maybe = mysql_store();                                                      \
  if (!maybe) GTEST_SKIP() << "DATAWARD_MYSQL_TEST not set or MySQL not built in"; \
  auto& store = *maybe

TEST(MySql, DialectIsReported) {
  REQUIRE_MYSQL(store);
  EXPECT_EQ(store.dialect(), dataward::Dialect::mysql);
}

TEST(MySql, EnsurePutGetRoundTrip) {
  REQUIRE_MYSQL(store);
  store.ensure<Span>();
  store.ensure<Span>();  // idempotent
  const auto day = std::chrono::year{2026} / std::chrono::month{9} / std::chrono::day{2};
  store.put(Span{"a", "hello", 42, 1.5, true, day, std::nullopt});

  auto s = store.get<Span>("a");
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->label, "hello");
  EXPECT_EQ(s->n, 42);
  EXPECT_DOUBLE_EQ(s->x, 1.5);
  EXPECT_TRUE(s->flag);
  EXPECT_EQ(s->day, day);
  EXPECT_EQ(s->note, std::nullopt);
}

TEST(MySql, UpsertWhereRemove) {
  REQUIRE_MYSQL(store);
  store.ensure<Span>();
  const auto day = std::chrono::year{2026} / std::chrono::month{1} / std::chrono::day{1};
  store.put_many<Span>({{"a", "x", 1, 0.0, false, day, std::nullopt},
                        {"b", "y", 2, 0.0, false, day, "kept"},
                        {"c", "y", 3, 0.0, false, day, std::nullopt}});
  store.put(Span{"a", "x2", 10, 0.0, false, day, std::nullopt});  // upsert

  EXPECT_EQ(store.get<Span>("a")->label, "x2");
  auto ys = store.where<Span>("label = :b0 ORDER BY n DESC", "y");
  ASSERT_EQ(ys.size(), 2u);
  EXPECT_EQ(ys[0].id, "c");
  EXPECT_EQ(ys[1].note, "kept");

  store.remove<Span>("b");
  EXPECT_EQ(store.all<Span>().size(), 2u);
}

TEST(MySql, TransactionRollsBack) {
  REQUIRE_MYSQL(store);
  store.ensure<Span>();
  const auto day = std::chrono::year{2026} / std::chrono::month{1} / std::chrono::day{1};
  {
    auto txn = store.begin();
    store.put(Span{"t", "gone", 0, 0.0, false, day, std::nullopt});
    // no commit
  }
  EXPECT_EQ(store.get<Span>("t"), std::nullopt);
}

}  // namespace
