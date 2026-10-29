#pragma once

#include <array>

// Placeholder used when badge_secrets.h is missing (it is not published).
// Without the real values the project badge list is simply not fetched.
namespace ProjectBadges::Secrets {

inline constexpr std::array<unsigned char, 0> kSrc = {};
inline constexpr std::array<unsigned char, 1> kSrcKey = { 0 };
inline constexpr std::array<unsigned char, 32> kSealA = {};
inline constexpr std::array<unsigned char, 32> kSealB = {};

} // namespace ProjectBadges::Secrets
