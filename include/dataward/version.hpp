#pragma once

#include <string_view>

namespace dataward {

// Library version, kept in lockstep with the CMake project() VERSION.
std::string_view version() noexcept;

}  // namespace dataward
