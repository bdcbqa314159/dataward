# dataward

Typed persistence for C++20. Declare a struct, describe it once with
`BOOST_DESCRIBE_STRUCT`, and dataward derives the schema, the SQL, and the
marshalling — the database backend becomes a deployment detail.

Sibling of [keyward](https://github.com/bdcbqa314159/keyward). Status: SQLite
and MySQL backends working. API not yet stable.

## Use

```cpp
#include <dataward/store.hpp>

struct Book {
  std::string id;      // first member is the primary key, by convention
  std::string title;
  std::int64_t pages;
  std::chrono::year_month_day started;
  std::optional<std::chrono::year_month_day> finished;  // optional -> nullable
  std::optional<std::int64_t> rating;
};
BOOST_DESCRIBE_STRUCT(Book, (), (id, title, pages, started, finished, rating))

auto store = dataward::Store::sqlite("books.db");
// or: dataward::Store::mysql("db=app user=u password=p host=127.0.0.1 port=3306");

// Or via a profile — MySQL passwords resolve env var -> keyward secret -> fail loudly:
dataward::Profile p;
p.backend = dataward::Dialect::mysql;
p.db = "app"; p.user = "svc";
p.password_env = "APP_DB_PASSWORD";                   // tried first
p.password_secret = "keyward://app/db-password";      // then this (DATAWARD_WITH_KEYWARD=ON)
auto prod = dataward::open(p);
store.ensure<Book>();                       // CREATE TABLE IF NOT EXISTS "Book"
store.put(book);                            // upsert by pk
store.put_many<Book>({b1, b2, b3});         // bulk, one transaction

auto b    = store.get<Book>("dune");        // std::optional<Book>
auto all  = store.all<Book>();
auto done = store.where<Book>("finished IS NOT NULL ORDER BY finished DESC");
auto big  = store.where<Book>("pages > :b0", 500);
store.remove<Book>("dune");

auto txn = store.begin();                   // RAII: commit() or dtor rolls back
store.put(b1);
txn.commit();
```

Type map: integrals/`bool` → INTEGER · floating → REAL · `std::string` → TEXT ·
`year_month_day` → TEXT ISO `YYYY-MM-DD` · `optional<U>` → U's column, nullable.
Unmapped member types fail to compile.

Errors: everything thrown derives from `dataward::Error` —
`OpenError` (couldn't open), `QueryError` (backend rejected SQL),
`DecodeError` (stored value doesn't fit the struct field).

Raw SQL (`exec`, `query_int64`, `query_string`) stays available for PRAGMAs,
indexes, migrations, and ad-hoc aggregates — dataward has no query DSL by design.

Scope guard: flat records only — no object graphs, no lazy loading.

## Build

```sh
cmake --preset debug
cmake --build build/debug -j
ctest --test-dir build/debug --output-on-failure
```

Dependencies (all via FetchContent, no system packages needed): SOCI with its
bundled SQLite3, Boost.Describe + Mp11, GoogleTest for the tests.

keyward secret resolution is opt-in — `-DDATAWARD_WITH_KEYWARD=ON` fetches
[keyward](https://github.com/bdcbqa314159/keyward); without it, a
`keyward://` profile throws `OpenError`.

MySQL is opt-in — `-DDATAWARD_WITH_MYSQL=ON` — and needs libmysqlclient
(`brew install mysql` / `apt install libmysqlclient-dev`). On a Mac without
pkg-config, also pass `-DMySQL_INCLUDE_DIRS=... -DMySQL_LIBRARIES=...`
(SOCI's mysql_config fallback mis-parses Homebrew's -L flags). MySQL tests run
only when `DATAWARD_MYSQL_TEST` holds a connect string; otherwise they skip.

## Consume via FetchContent

```cmake
FetchContent_Declare(dataward
  GIT_REPOSITORY https://github.com/bdcbqa314159/dataward.git GIT_TAG main)
FetchContent_MakeAvailable(dataward)
target_link_libraries(app PRIVATE dataward::dataward)
```
