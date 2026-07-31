#pragma once

#include <array>
#include <cstdint>
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

// Decode at least two equal 48-bit PWM frames from ESPHome remote_receiver
// timings. Both electrical polarities are accepted.
bool decode_activlink(const std::vector<int32_t> &timings, ActivLinkFrame *frame);

}  // namespace esphome::honeywell_activlink

