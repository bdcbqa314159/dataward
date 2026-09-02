# dataward

Typed persistence for C++20. Declare a struct, describe it once with
`BOOST_DESCRIBE_STRUCT`, and dataward derives the schema, the SQL, and the
marshalling — the database backend becomes a deployment detail.

Sibling of [keyward](https://github.com/bdcbqa314159/keyward). Status: SQLite
backend complete; MySQL backend planned. API not yet stable.

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

## Consume via FetchContent

```cmake
FetchContent_Declare(dataward
  GIT_REPOSITORY https://github.com/bdcbqa314159/dataward.git GIT_TAG main)
FetchContent_MakeAvailable(dataward)
target_link_libraries(app PRIVATE dataward::dataward)
```
