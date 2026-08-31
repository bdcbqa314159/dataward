# dataward

Typed persistence for C++20. Declare a struct, describe it once with
`BOOST_DESCRIBE_STRUCT`, and dataward derives the schema, the SQL, and the JSON —
the database backend (SQLite, MySQL) becomes a deployment detail.

Sibling of [keyward](../keyward). Status: early — API not stable.

## Sketch (target API)

```cpp
struct Book {
  std::string id;      // first field is the primary key, by convention
  std::string title;
  int pages;
};
BOOST_DESCRIBE_STRUCT(Book, (), (id, title, pages))

auto store = dataward::open(profile);   // named profile: SQLite path or MySQL creds
store.ensure<Book>();                   // CREATE TABLE IF NOT EXISTS
store.put(book);
auto b = store.get<Book>("dune");
auto all = store.all<Book>();
```

Scope guard: flat records only — no object graphs, no lazy loading, no query DSL.

## Build

```sh
cmake --preset debug
cmake --build build/debug -j
ctest --test-dir build/debug --output-on-failure
```

## Consume via FetchContent

```cmake
FetchContent_Declare(dataward GIT_REPOSITORY <url> GIT_TAG main)
FetchContent_MakeAvailable(dataward)
target_link_libraries(app PRIVATE dataward::dataward)
```
