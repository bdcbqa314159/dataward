#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "dataward/store.hpp"

#ifdef DATAWARD_TESTS_HAVE_KEYWARD
#include <keyward/default_store.hpp>
#endif

namespace {

struct Note {
  std::string id;
  std::string body;
};
BOOST_DESCRIBE_STRUCT(Note, (), (id, body))

TEST(Profile, SqliteProfileOpensAndWorks) {
  const auto path = std::filesystem::path(testing::TempDir()) / "profile.db";
  std::filesystem::remove(path);

  dataward::Profile p;
  p.path = path.string();
  auto store = dataward::open(p);
  EXPECT_EQ(store.dialect(), dataward::Dialect::sqlite);
  store.ensure<Note>();
  store.put(Note{"a", "hello"});
  EXPECT_EQ(store.get<Note>("a")->body, "hello");
}

TEST(Profile, SqliteWithoutPathThrows) {
  EXPECT_THROW(dataward::open(dataward::Profile{}), dataward::OpenError);
}

TEST(Profile, UnsetPasswordEnvFailsLoudly) {
  dataward::Profile p;
  p.backend = dataward::Dialect::mysql;
  p.db = "x";
  p.user = "u";
  p.password_env = "DATAWARD_NO_SUCH_VAR_12345";
  EXPECT_THROW(dataward::open(p), dataward::OpenError);
}

TEST(Profile, NonKeywardSecretSchemeThrows) {
  dataward::Profile p;
  p.backend = dataward::Dialect::mysql;
  p.db = "x";
  p.user = "u";
  p.password_secret = "vault://a/b";
  EXPECT_THROW(dataward::open(p), dataward::OpenError);
}

TEST(Profile, MalformedKeywardUriThrows) {
  dataward::Profile p;
  p.backend = dataward::Dialect::mysql;
  p.db = "x";
  p.user = "u";
  p.password_secret = "keyward://no-slash";
  EXPECT_THROW(dataward::open(p), dataward::OpenError);
}

// Live MySQL through a profile, password via the env chain. In CI
// DATAWARD_MYSQL_PASSWORD is exported; locally (passwordless root) it is
// absent and the profile carries no password source.
TEST(Profile, MysqlProfileConnects) {
  if (std::getenv("DATAWARD_MYSQL_TEST") == nullptr) GTEST_SKIP() << "DATAWARD_MYSQL_TEST not set";

  dataward::Profile p;
  p.backend = dataward::Dialect::mysql;
  p.db = "dataward_test";
  p.user = "root";
  if (std::getenv("DATAWARD_MYSQL_PASSWORD") != nullptr) p.password_env = "DATAWARD_MYSQL_PASSWORD";

  auto store = dataward::open(p);
  EXPECT_EQ(store.dialect(), dataward::Dialect::mysql);
  store.exec("DROP TABLE IF EXISTS \"Note\"");
  store.ensure<Note>();
  store.put(Note{"p", "via profile"});
  EXPECT_EQ(store.get<Note>("p")->body, "via profile");
}

#ifdef DATAWARD_TESTS_HAVE_KEYWARD
// Full chain live: seed a secret through keyward, then open MySQL with
// password_secret. Gated: DATAWARD_KEYWARD_MYSQL_TEST holds the password of a
// local MySQL user 'dataward_kw' with rights on dataward_test.
TEST(Profile, KeywardSecretResolvesLive) {
  const char* pw = std::getenv("DATAWARD_KEYWARD_MYSQL_TEST");
  if (pw == nullptr) GTEST_SKIP() << "DATAWARD_KEYWARD_MYSQL_TEST not set";

  keyward::defaultSecretStore("dataward-test")->set("mysql-pw", pw);

  dataward::Profile p;
  p.backend = dataward::Dialect::mysql;
  p.db = "dataward_test";
  p.user = "dataward_kw";
  p.password_secret = "keyward://dataward-test/mysql-pw";

  auto store = dataward::open(p);
  store.exec("DROP TABLE IF EXISTS \"Note\"");
  store.ensure<Note>();
  store.put(Note{"kw", "via keyward"});
  EXPECT_EQ(store.get<Note>("kw")->body, "via keyward");
}
#endif

}  // namespace
