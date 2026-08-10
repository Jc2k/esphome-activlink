#include "protocol.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <limits>

namespace esphome::honeywell_activlink {
namespace {

constexpr uint32_t SHORT_MIN_US = 100;
constexpr uint32_t SHORT_MAX_US = 240;
constexpr uint32_t LONG_MIN_US = 250;
constexpr uint32_t LONG_MAX_US = 410;
constexpr uint32_t SYNC_MIN_US = 400;
constexpr uint32_t SYNC_MAX_US = 650;
constexpr size_t FRAME_BITS = 48;
constexpr uint8_t MINIMUM_REPEATS = 4;

struct FrameCount {
  ActivLinkFrame frame;
  uint8_t count;
};

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

size_t decode_activlink_rows(const std::vector<int32_t> &timings,
                            std::vector<ActivLinkFrame> *frames) {
  frames->clear();
  for (size_t start = 0; start < timings.size(); start++) {
    ActivLinkFrame candidate{};
    if (!decode_at(timings, start, &candidate)) {
      continue;
    }
    frames->push_back(candidate);
  }
  return frames->size();
}

bool decode_activlink(const std::vector<int32_t> &timings, ActivLinkFrame *frame) {
  std::vector<ActivLinkFrame> rows;
  decode_activlink_rows(timings, &rows);
  std::vector<FrameCount> candidates;

  for (const auto &candidate : rows) {
    bool found = false;
    for (auto &entry : candidates) {
      if (!(entry.frame == candidate)) {
        continue;
      }
      found = true;
      entry.count++;
      if (entry.count >= MINIMUM_REPEATS) {
        *frame = candidate;
        return true;
      }
      break;
    }

    if (!found) {
      candidates.push_back({candidate, 1});
    }
  }
  return false;
}

void ActivLinkBurstDecoder::reset() {
  for (auto &candidate : this->candidates_) {
    candidate = {};
  }
}

bool ActivLinkBurstDecoder::decode(const std::vector<int32_t> &timings,
                                  uint32_t now, ActivLinkFrame *frame,
                                  size_t *candidate_rows) {
  std::vector<ActivLinkFrame> rows;
  decode_activlink_rows(timings, &rows);
  if (candidate_rows != nullptr) {
    *candidate_rows = rows.size();
  }

  // The asynchronous CC1101 output also produces captures for unrelated RF
  // activity. Those captures must not keep an already-emitted button frame
  // alive indefinitely. Expire consensus per decoded frame, before considering
  // the rows in this capture.
  for (auto &candidate : this->candidates_) {
    if (candidate.count != 0 && now - candidate.last_seen_ms > BURST_GAP_MS) {
      candidate = {};
    }
  }

  for (const auto &row : rows) {
    Candidate *match = nullptr;
    Candidate *replacement = &this->candidates_[0];
    for (auto &candidate : this->candidates_) {
      if (candidate.count != 0 && candidate.frame == row) {
        match = &candidate;
        break;
      }
      if (candidate.count == 0 || candidate.count < replacement->count) {
        replacement = &candidate;
      }
    }

    if (match == nullptr) {
      match = replacement;
      *match = {row, 1, false, now};
    } else if (match->count < std::numeric_limits<uint8_t>::max()) {
      match->count++;
    }
    match->last_seen_ms = now;

    if (match->count >= MINIMUM_REPEATS && !match->emitted) {
      match->emitted = true;
      *frame = row;
      return true;
    }
  }
  return false;
}

std::string summarize_activlink_capture(const std::vector<int32_t> &timings) {
  size_t short_count = 0;
  size_t long_count = 0;
  size_t sync_count = 0;
  size_t other_count = 0;
  for (int32_t timing : timings) {
    if (is_short(timing)) {
      short_count++;
    } else if (is_long(timing)) {
      long_count++;
    } else if (is_sync(timing)) {
      sync_count++;
    } else {
      other_count++;
    }
  }

  char counts[112];
  std::snprintf(counts, sizeof(counts), "n=%u short=%u long=%u sync=%u other=%u head=",
                static_cast<unsigned>(timings.size()), static_cast<unsigned>(short_count),
                static_cast<unsigned>(long_count), static_cast<unsigned>(sync_count),
                static_cast<unsigned>(other_count));
  std::string summary{counts};

  const size_t head_size = std::min<size_t>(timings.size(), 12);
  for (size_t i = 0; i < head_size; i++) {
    char timing[18];
    std::snprintf(timing, sizeof(timing), "%s%" PRId32, i == 0 ? "" : ",", timings[i]);
    summary.append(timing);
  }
  return summary;
}

}  // namespace esphome::honeywell_activlink
