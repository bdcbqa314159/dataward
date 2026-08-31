#include "dataward/version.hpp"

#include <gtest/gtest.h>

TEST(Version, MatchesProjectVersion) { EXPECT_EQ(dataward::version(), "0.1.0"); }
