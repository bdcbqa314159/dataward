#pragma once

#include <stdexcept>

namespace dataward {

// Base for everything dataward throws — catch this to catch them all.
struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Opening/creating the database failed (path, permissions, credentials).
struct OpenError : Error {
  using Error::Error;
};

// The backend rejected a statement (syntax, constraint, missing table).
struct QueryError : Error {
  using Error::Error;
};

// A stored value would not convert to the struct field's type.
struct DecodeError : Error {
  using Error::Error;
};

}  // namespace dataward
