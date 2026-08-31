#include <gtest/gtest.h>

#include <filesystem>

#include "dataward/store.hpp"

namespace {

std::string temp_db_path(const char* name) {
  return (std::filesystem::path(testing::TempDir()) / name).string();
}

TEST(StoreRoundtrip, WriteThenReadBack) {
  const auto path = temp_db_path("roundtrip.db");
  std::filesystem::remove(path);

  auto store = dataward::Store::sqlite(path);
  store.exec("CREATE TABLE kv (k TEXT PRIMARY KEY, i INTEGER, s TEXT)");
  store.exec("INSERT INTO kv VALUES ('answer', 42, 'forty-two')");

  EXPECT_EQ(store.query_int64("SELECT i FROM kv WHERE k = 'answer'"), 42);
  EXPECT_EQ(store.query_string("SELECT s FROM kv WHERE k = 'answer'"), "forty-two");
  EXPECT_EQ(store.query_int64("SELECT i FROM kv WHERE k = 'missing'"), std::nullopt);
}

TEST(StoreRoundtrip, SurvivesReopen) {
  const auto path = temp_db_path("reopen.db");
  std::filesystem::remove(path);

  {
    auto store = dataward::Store::sqlite(path);
    store.exec("CREATE TABLE t (v INTEGER)");
    store.exec("INSERT INTO t VALUES (7)");
  }
  auto store = dataward::Store::sqlite(path);
  EXPECT_EQ(store.query_int64("SELECT v FROM t"), 7);
}

TEST(StoreRoundtrip, NullComesBackEmpty) {
  const auto path = temp_db_path("nulls.db");
  std::filesystem::remove(path);

  auto store = dataward::Store::sqlite(path);
  store.exec("CREATE TABLE t (v INTEGER)");
  store.exec("INSERT INTO t VALUES (NULL)");
  EXPECT_EQ(store.query_int64("SELECT v FROM t"), std::nullopt);
}

}  // namespace
