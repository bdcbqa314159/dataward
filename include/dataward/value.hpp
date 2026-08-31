#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace dataward {

// Wire value crossing the backend seam: everything any backend can bind or
// return. monostate is SQL NULL.
using Value = std::variant<std::monostate, std::int64_t, double, std::string>;

}  // namespace dataward
