#include "components/honeywell_activlink/protocol.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using esphome::honeywell_activlink::ActivLinkFrame;
using esphome::honeywell_activlink::decode_activlink;

namespace {

std::vector<int32_t> encode(const std::array<uint8_t, 6> &bytes, unsigned repeats = 2,
                            bool inverted = false) {
  std::vector<int32_t> timings;
  timings.push_back(inverted ? -80 : 80);  // Leading receiver noise.
  for (unsigned repeat = 0; repeat < repeats; repeat++) {
    timings.push_back(inverted ? 480 : -480);
    for (uint8_t byte : bytes) {
      for (uint8_t mask = 0x80; mask != 0; mask >>= 1U) {
        const bool bit = (byte & mask) != 0;
        const int32_t first = bit ? 320 : 160;
        const int32_t second = bit ? -160 : -320;
        timings.push_back(inverted ? -first : first);
        timings.push_back(inverted ? -second : second);
      }
    }
    timings.push_back(inverted ? -480 : 480);
  }
  timings.push_back(inverted ? 3000 : -3000);
  return timings;
}

void test_normal_press() {
  // Captured from the project's first physical transmitter.
  const std::array<uint8_t, 6> bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  ActivLinkFrame frame{};
  assert(decode_activlink(encode(bytes), &frame));
  assert(frame.bytes == bytes);
  assert(frame.device_id() == 0xFB100);
  assert(frame.device_type() == 2);
  assert(!frame.secret_press());
  assert(!frame.battery_low());
}

void test_inverted_secret_press() {
  // Also captured from the physical transmitter; invert the synthetic timings
  // to exercise the decoder's polarity-independent path.
  const std::array<uint8_t, 6> bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x10};
  ActivLinkFrame frame{};
  assert(decode_activlink(encode(bytes, 3, true), &frame));
  assert(frame.secret_press());
  assert(!frame.battery_low());
}

void test_secret_low_battery_press() {
  // Derived from the observed secret frame with LOWBAT set and parity adjusted.
  const std::array<uint8_t, 6> bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x13};
  ActivLinkFrame frame{};
  assert(decode_activlink(encode(bytes), &frame));
  assert(frame.secret_press());
  assert(frame.battery_low());
}

void test_requires_repetition() {
  const std::array<uint8_t, 6> bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  ActivLinkFrame frame{};
  assert(!decode_activlink(encode(bytes, 1), &frame));
}

void test_rejects_bad_parity() {
  const std::array<uint8_t, 6> bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x00};
  ActivLinkFrame frame{};
  assert(!decode_activlink(encode(bytes), &frame));
}

}  // namespace

int main() {
  test_normal_press();
  test_inverted_secret_press();
  test_secret_low_battery_press();
  test_requires_repetition();
  test_rejects_bad_parity();
}
