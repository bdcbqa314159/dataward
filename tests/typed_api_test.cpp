#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "dataward/store.hpp"

namespace {

struct Book {
  std::string id;  // first field is the primary key, by convention
  std::string title;
  std::int64_t pages;
  double rating;
  bool finished;
};
BOOST_DESCRIBE_STRUCT(Book, (), (id, title, pages, rating, finished))

dataward::Store fresh_store(const char* name) {
  const auto path = std::filesystem::path(testing::TempDir()) / name;
  std::filesystem::remove(path);
  return dataward::Store::sqlite(path.string());
}

TEST(TypedApi, EnsureCreatesTableNamedAfterStruct) {
  auto store = fresh_store("ensure.db");
  store.ensure<Book>();
  store.ensure<Book>();  // idempotent
  EXPECT_EQ(store.query_string("SELECT name FROM sqlite_master WHERE type='table' AND name='Book'"),
            "Book");
}

TEST(TypedApi, PutGetRoundTrip) {
  auto store = fresh_store("roundtrip_typed.db");
  store.ensure<Book>();
  store.put(Book{"dune", "Dune", 412, 4.5, true});

  auto b = store.get<Book>("dune");
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->id, "dune");
  EXPECT_EQ(b->title, "Dune");
  EXPECT_EQ(b->pages, 412);
  EXPECT_DOUBLE_EQ(b->rating, 4.5);
  EXPECT_TRUE(b->finished);
}

TEST(TypedApi, PutSamePkIsUpsert) {
  auto store = fresh_store("upsert.db");
  store.ensure<Book>();
  store.put(Book{"dune", "Dune", 412, 4.5, false});
  store.put(Book{"dune", "Dune (reread)", 412, 5.0, true});

  auto b = store.get<Book>("dune");
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->title, "Dune (reread)");
  EXPECT_DOUBLE_EQ(b->rating, 5.0);
  EXPECT_TRUE(b->finished);
  EXPECT_EQ(store.query_int64("SELECT COUNT(*) FROM \"Book\""), 1);
}

TEST(TypedApi, GetMissingReturnsNullopt) {
  auto store = fresh_store("missing.db");
  store.ensure<Book>();
  EXPECT_EQ(store.get<Book>("nope"), std::nullopt) << "expected no row";
}

TEST(TypedApi, ValuesAreBoundNotInterpolated) {
  auto store = fresh_store("binds.db");
  store.ensure<Book>();
  const std::string hostile = "O'Brien\"; DROP TABLE \"Book\"; --";
  store.put(Book{"k", hostile, 1, 0.0, false});

  auto b = store.get<Book>("k");
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->title, hostile);
  EXPECT_EQ(store.query_string("SELECT name FROM sqlite_master WHERE type='table' AND name='Book'"),
            "Book");
}

}  // namespace
