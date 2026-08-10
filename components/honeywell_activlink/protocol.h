#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::honeywell_activlink {

struct ActivLinkFrame {
  std::array<uint8_t, 6> bytes{};

  uint32_t device_id() const {
    return (static_cast<uint32_t>(bytes[0]) << 12U) |
           (static_cast<uint32_t>(bytes[1]) << 4U) |
           (static_cast<uint32_t>(bytes[2]) >> 4U);
  }
  uint8_t device_type() const { return (bytes[3] & 0x70U) >> 4U; }
  bool secret_press() const { return (bytes[5] & 0x10U) != 0; }
  bool relay() const { return (bytes[5] & 0x08U) != 0; }
  bool battery_low() const { return (bytes[5] & 0x02U) != 0; }

  bool operator==(const ActivLinkFrame &other) const { return bytes == other.bytes; }
};

// Extract every parity-valid 48-bit PWM row from one RMT capture. ActivLink
// repeats a row across a broadcast, but the RMT driver may split that broadcast
// across several captures.
size_t decode_activlink_rows(const std::vector<int32_t> &timings,
                            std::vector<ActivLinkFrame> *frames);

// Decode at least four equal rows from one capture. Kept as a stateless helper
// for protocol tests and callers that already own a complete RF burst.
bool decode_activlink(const std::vector<int32_t> &timings, ActivLinkFrame *frame);

// Accumulate row consensus across adjacent RMT captures. A long gap between
// sightings of the same frame starts a new burst; within a burst, each
// distinct frame is emitted at most once. Rejected captures and other valid
// frames do not extend a frame's burst lifetime.
class ActivLinkBurstDecoder {
 public:
  bool decode(const std::vector<int32_t> &timings, uint32_t now,
              ActivLinkFrame *frame, size_t *candidate_rows = nullptr);
  void reset();

 protected:
  static constexpr size_t MAX_CANDIDATES = 8;
  static constexpr uint32_t BURST_GAP_MS = 1500;
  static constexpr uint8_t MINIMUM_REPEATS = 4;

  struct Candidate {
    ActivLinkFrame frame{};
    uint8_t count{0};
    bool emitted{false};
    uint32_t last_seen_ms{0};
  };

  std::array<Candidate, MAX_CANDIDATES> candidates_{};
};

// Bounded, non-secret timing summary for Home Assistant diagnostics. It is
// intentionally compact enough for one text-sensor state.
std::string summarize_activlink_capture(const std::vector<int32_t> &timings);

}  // namespace esphome::honeywell_activlink
