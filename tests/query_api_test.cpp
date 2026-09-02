#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "dataward/store.hpp"

namespace {

struct Track {
  std::string id;
  std::string artist;
  std::int64_t plays;
};
BOOST_DESCRIBE_STRUCT(Track, (), (id, artist, plays))

dataward::Store fresh_store(const char* name) {
  const auto path = std::filesystem::path(testing::TempDir()) / name;
  std::filesystem::remove(path);
  return dataward::Store::sqlite(path.string());
}

dataward::Store seeded(const char* name) {
  auto store = fresh_store(name);
  store.ensure<Track>();
  store.put_many<Track>({{"t1", "Kraftwerk", 10},
                         {"t2", "Autechre", 25},
                         {"t3", "Kraftwerk", 40},
                         {"t4", "Aphex Twin", 5}});
  return store;
}

TEST(QueryApi, AllReturnsEveryRow) {
  auto store = seeded("all.db");
  EXPECT_EQ(store.all<Track>().size(), 4u);
}

TEST(QueryApi, WhereFiltersAndOrders) {
  auto store = seeded("where.db");
  auto hits = store.where<Track>("plays >= :b0 ORDER BY plays DESC", 10);
  ASSERT_EQ(hits.size(), 3u);
  EXPECT_EQ(hits[0].id, "t3");
  EXPECT_EQ(hits[1].id, "t2");
  EXPECT_EQ(hits[2].id, "t1");
}

TEST(QueryApi, WhereBindsStrings) {
  auto store = seeded("where_str.db");
  auto hits = store.where<Track>("artist = :b0 ORDER BY id", "Kraftwerk");
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].id, "t1");
  EXPECT_EQ(hits[1].id, "t3");
}

TEST(QueryApi, WhereWithNoMatchIsEmpty) {
  auto store = seeded("where_none.db");
  EXPECT_TRUE(store.where<Track>("plays > :b0", 1000).empty());
}

TEST(QueryApi, RemoveDeletesByPk) {
  auto store = seeded("remove.db");
  store.remove<Track>("t2");
  EXPECT_EQ(store.all<Track>().size(), 3u);
  EXPECT_EQ(store.get<Track>("t2"), std::nullopt);
  store.remove<Track>("t2");  // absent pk is a no-op
  EXPECT_EQ(store.all<Track>().size(), 3u);
}

TEST(QueryApi, TransactionCommits) {
  auto store = fresh_store("txn_commit.db");
  store.ensure<Track>();
  {
    auto txn = store.begin();
    store.put(Track{"a", "X", 1});
    txn.commit();
  }
  EXPECT_EQ(store.all<Track>().size(), 1u);
}

TEST(QueryApi, TransactionRollsBackOnDestruction) {
  auto store = fresh_store("txn_rollback.db");
  store.ensure<Track>();
  {
    auto txn = store.begin();
    store.put(Track{"a", "X", 1});
    // no commit -> rollback
  }
  EXPECT_TRUE(store.all<Track>().empty());
}

}  // namespace
