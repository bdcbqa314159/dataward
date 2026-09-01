#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "dataward/store.hpp"

namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::year;
using std::chrono::year_month_day;

// Deliberately shaped like bookward's Book — the first real consumer.
struct Reading {
  std::string id;
  std::string title;
  std::int64_t pages;
  year_month_day started;
  std::optional<year_month_day> finished;
  std::optional<std::string> notes;
  std::optional<std::int64_t> rating;
};
BOOST_DESCRIBE_STRUCT(Reading, (), (id, title, pages, started, finished, notes, rating))

dataward::Store fresh_store(const char* name) {
  const auto path = std::filesystem::path(testing::TempDir()) / name;
  std::filesystem::remove(path);
  return dataward::Store::sqlite(path.string());
}

TEST(OptionalDate, OptionalColumnsAreNullable) {
  auto store = fresh_store("nullable.db");
  store.ensure<Reading>();
  // NULLs in every optional column must insert cleanly (NOT NULL would reject).
  store.put(Reading{"a", "A", 1, year{2026} / month{1} / day{2}, std::nullopt, std::nullopt,
                    std::nullopt});

  auto r = store.get<Reading>("a");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->finished, std::nullopt);
  EXPECT_EQ(r->notes, std::nullopt);
  EXPECT_EQ(r->rating, std::nullopt);
}

TEST(OptionalDate, SetOptionalsRoundTrip) {
  auto store = fresh_store("set_optionals.db");
  store.ensure<Reading>();
  const auto done = year{2026} / month{8} / day{31};
  store.put(Reading{"b", "B", 2, year{2026} / month{2} / day{10}, done, "great", 5});

  auto r = store.get<Reading>("b");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->finished, done);
  EXPECT_EQ(r->notes, "great");
  EXPECT_EQ(r->rating, 5);
}

TEST(OptionalDate, DateStoredAsIsoText) {
  auto store = fresh_store("iso.db");
  store.ensure<Reading>();
  store.put(Reading{"c", "C", 3, year{2026} / month{9} / day{1}, std::nullopt, std::nullopt,
                    std::nullopt});
  EXPECT_EQ(store.query_string("SELECT started FROM \"Reading\" WHERE id = 'c'"), "2026-09-01");

  auto r = store.get<Reading>("c");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->started, year{2026} / month{9} / day{1});
}

TEST(OptionalDate, UpsertCanClearAnOptional) {
  auto store = fresh_store("clear.db");
  store.ensure<Reading>();
  store.put(Reading{"d", "D", 4, year{2026} / month{3} / day{3}, std::nullopt, "wip", 3});
  store.put(Reading{"d", "D", 4, year{2026} / month{3} / day{3}, std::nullopt, std::nullopt,
                    std::nullopt});

  auto r = store.get<Reading>("d");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->notes, std::nullopt);
  EXPECT_EQ(r->rating, std::nullopt);
}

}  // namespace
