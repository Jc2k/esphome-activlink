#include "protocol.h"

#include <cstddef>

namespace esphome::honeywell_activlink {
namespace {

constexpr uint32_t SHORT_MIN_US = 100;
constexpr uint32_t SHORT_MAX_US = 240;
constexpr uint32_t LONG_MIN_US = 250;
constexpr uint32_t LONG_MAX_US = 410;
constexpr uint32_t SYNC_MIN_US = 400;
constexpr uint32_t SYNC_MAX_US = 650;
constexpr size_t FRAME_BITS = 48;

uint32_t duration_us(int32_t timing) {
  return timing < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(timing))
                    : static_cast<uint32_t>(timing);
}

bool is_short(int32_t timing) {
  const uint32_t duration = duration_us(timing);
  return duration >= SHORT_MIN_US && duration <= SHORT_MAX_US;
}

bool is_long(int32_t timing) {
  const uint32_t duration = duration_us(timing);
  return duration >= LONG_MIN_US && duration <= LONG_MAX_US;
}

bool is_sync(int32_t timing) {
  const uint32_t duration = duration_us(timing);
  return duration >= SYNC_MIN_US && duration <= SYNC_MAX_US;
}

bool same_level(int32_t lhs, int32_t rhs) { return (lhs >= 0) == (rhs >= 0); }

bool has_even_parity(const std::array<uint8_t, 6> &bytes) {
  uint8_t parity = 0;
  for (uint8_t byte : bytes) {
    while (byte != 0) {
      parity ^= byte & 1U;
      byte >>= 1U;
    }
  }
  return parity == 0;
}

bool is_sane(const ActivLinkFrame &frame) {
  bool all_zero = true;
  bool all_one = true;
  for (uint8_t byte : frame.bytes) {
    all_zero &= byte == 0x00;
    all_one &= byte == 0xFF;
  }
  return !all_zero && !all_one && has_even_parity(frame.bytes);
}

bool decode_at(const std::vector<int32_t> &timings, size_t start, ActivLinkFrame *frame) {
  // One preamble timing, 48 mark/space pairs, and one postamble timing.
  if (start + 1U + FRAME_BITS * 2U >= timings.size() || !is_sync(timings[start])) {
    return false;
  }

  const bool preamble_level = timings[start] >= 0;
  ActivLinkFrame candidate{};
  size_t cursor = start + 1U;

  for (size_t bit = 0; bit < FRAME_BITS; bit++, cursor += 2U) {
    const int32_t first = timings[cursor];
    const int32_t second = timings[cursor + 1U];
    if ((first >= 0) == preamble_level || (second >= 0) != preamble_level ||
        same_level(first, second)) {
      return false;
    }

    bool value;
    if (is_short(first) && is_long(second)) {
      value = false;
    } else if (is_long(first) && is_short(second)) {
      value = true;
    } else {
      return false;
    }

    candidate.bytes[bit / 8U] =
        static_cast<uint8_t>((candidate.bytes[bit / 8U] << 1U) | (value ? 1U : 0U));
  }

  if ((timings[cursor] >= 0) == preamble_level || !is_sync(timings[cursor])) {
    return false;
  }
  if (!is_sane(candidate)) {
    return false;
  }

  *frame = candidate;
  return true;
}

}  // namespace

bool decode_activlink(const std::vector<int32_t> &timings, ActivLinkFrame *frame) {
  ActivLinkFrame previous{};
  bool have_previous = false;
  uint8_t repeat_count = 0;

  for (size_t start = 0; start < timings.size(); start++) {
    ActivLinkFrame candidate{};
    if (!decode_at(timings, start, &candidate)) {
      continue;
    }

    if (have_previous && candidate == previous) {
      repeat_count++;
    } else {
      previous = candidate;
      have_previous = true;
      repeat_count = 1;
    }

    if (repeat_count >= 2) {
      *frame = candidate;
      return true;
    }
  }
  return false;
}

}  // namespace esphome::honeywell_activlink

