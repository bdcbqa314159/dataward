#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "dataward/store.hpp"

namespace {

struct Item {
  std::string id;
  std::int64_t n;
};
BOOST_DESCRIBE_STRUCT(Item, (), (id, n))

struct Dated {
  std::string id;
  std::chrono::year_month_day when;
};
BOOST_DESCRIBE_STRUCT(Dated, (), (id, when))

dataward::Store fresh_store(const char* name) {
  const auto path = std::filesystem::path(testing::TempDir()) / name;
  std::filesystem::remove(path);
  return dataward::Store::sqlite(path.string());
}

TEST(Errors, UnopenablePathThrowsOpenError) {
  EXPECT_THROW(dataward::Store::sqlite("/nonexistent-dir-dataward/x.db"), dataward::OpenError);
}

TEST(Errors, BadSqlThrowsQueryError) {
  auto store = fresh_store("bad_sql.db");
  EXPECT_THROW(store.exec("THIS IS NOT SQL"), dataward::QueryError);
}

TEST(Errors, MissingTableThrowsQueryError) {
  auto store = fresh_store("no_table.db");
  EXPECT_THROW(store.get<Item>("x"), dataward::QueryError);
}

TEST(Errors, TypeMismatchThrowsDecodeError) {
  auto store = fresh_store("mismatch.db");
  // A column declared TEXT feeding an int64 member: the schema drifted from
  // the struct. (Text in an INTEGER-declared column is silently coerced by
  // SQLite before dataward ever sees it, so that is not the mismatch case.)
  store.exec("CREATE TABLE \"Item\" (id TEXT PRIMARY KEY, n TEXT)");
  store.exec("INSERT INTO \"Item\" VALUES ('a', 'not-a-number')");
  EXPECT_THROW(store.get<Item>("a"), dataward::DecodeError);
}

TEST(Errors, BadStoredDateThrowsDecodeError) {
  auto store = fresh_store("bad_date.db");
  store.ensure<Dated>();
  store.exec("INSERT INTO \"Dated\" VALUES ('a', 'not-a-date')");
  EXPECT_THROW(store.get<Dated>("a"), dataward::DecodeError);
}

TEST(Errors, AllCatchableAsBaseError) {
  auto store = fresh_store("base.db");
  try {
    store.exec("NOPE");
    FAIL() << "expected a throw";
  } catch (const dataward::Error& e) {
    EXPECT_NE(std::string(e.what()).find("dataward:"), std::string::npos);
  }
}

TEST(Errors, RollbackDuringUnwindDoesNotTerminate) {
  auto store = fresh_store("unwind.db");
  store.ensure<Item>();
  EXPECT_THROW(
      {
        auto txn = store.begin();
        store.put(Item{"a", 1});
        store.exec("NOT SQL");  // throws; txn dtor rolls back during unwind
      },
      dataward::QueryError);
  EXPECT_TRUE(store.all<Item>().empty()) << "the aborted transaction must not persist";
}

}  // namespace
