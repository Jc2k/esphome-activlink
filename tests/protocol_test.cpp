#include "components/honeywell_activlink/protocol.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using esphome::honeywell_activlink::ActivLinkFrame;
using esphome::honeywell_activlink::decode_activlink;

namespace {

using FrameBytes = std::array<uint8_t, 6>;

void append_frame_symbols(const FrameBytes &bytes, std::vector<bool> *symbols) {
  // ActivLink sends three LOW preamble symbols. Each data bit is HLL for 0 or
  // HHL for 1, followed by three HIGH postamble symbols.
  symbols->insert(symbols->end(), 3, false);
  for (uint8_t byte : bytes) {
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1U) {
      const bool bit = (byte & mask) != 0;
      symbols->push_back(true);
      symbols->push_back(bit);
      symbols->push_back(false);
    }
  }
  symbols->insert(symbols->end(), 3, true);
}

std::vector<int32_t> encode(const std::vector<FrameBytes> &frames, bool inverted = false) {
  std::vector<bool> symbols;
  for (const auto &frame : frames) {
    append_frame_symbols(frame, &symbols);
  }

  std::vector<int32_t> timings;
  timings.push_back(inverted ? -80 : 80);  // Leading receiver noise.
  bool level = symbols.front();
  uint32_t run_symbols = 0;
  for (bool symbol : symbols) {
    if (symbol == level) {
      run_symbols++;
      continue;
    }
    const int32_t duration = static_cast<int32_t>(run_symbols * 160U);
    timings.push_back((level != inverted) ? duration : -duration);
    level = symbol;
    run_symbols = 1;
  }
  const int32_t duration = static_cast<int32_t>(run_symbols * 160U);
  timings.push_back((level != inverted) ? duration : -duration);
  timings.push_back(inverted ? 3000 : -3000);
  return timings;
}

std::vector<int32_t> encode(const FrameBytes &bytes, unsigned repeats = 4,
                            bool inverted = false) {
  return encode(std::vector<FrameBytes>(repeats, bytes), inverted);
}

void test_normal_press() {
  // Captured from the project's first physical transmitter.
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  ActivLinkFrame frame{};
  assert(decode_activlink(encode(bytes), &frame));
  assert(frame.bytes == bytes);
  assert(frame.device_id() == 0xFB100);
  assert(frame.device_type() == 2);
  assert(!frame.secret_press());
  assert(!frame.battery_low());
}

void test_inverted_secret_press() {
  // Also captured from the physical transmitter; invert the demodulated FSK
  // symbol stream to exercise the decoder's polarity-independent path.
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x10};
  ActivLinkFrame frame{};
  assert(decode_activlink(encode(bytes, 4, true), &frame));
  assert(frame.secret_press());
  assert(!frame.battery_low());
}

void test_secret_low_battery_press() {
  // Derived from the observed secret frame with LOWBAT set and parity adjusted.
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x13};
  ActivLinkFrame frame{};
  assert(decode_activlink(encode(bytes), &frame));
  assert(frame.secret_press());
  assert(frame.battery_low());
}

void test_requires_four_repetitions() {
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  ActivLinkFrame frame{};
  assert(!decode_activlink(encode(bytes, 3), &frame));
  assert(decode_activlink(encode(bytes, 4), &frame));
}

void test_matching_repetitions_need_not_be_consecutive() {
  const FrameBytes normal{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  const FrameBytes secret{0xFB, 0x10, 0x00, 0x20, 0x00, 0x10};
  ActivLinkFrame frame{};
  assert(decode_activlink(encode({normal, normal, secret, normal, normal}), &frame));
  assert(frame.bytes == normal);
}

void test_rejects_bad_parity() {
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x00};
  ActivLinkFrame frame{};
  assert(!decode_activlink(encode(bytes), &frame));
}

}  // namespace

int main() {
  test_normal_press();
  test_inverted_secret_press();
  test_secret_low_battery_press();
  test_requires_four_repetitions();
  test_matching_repetitions_need_not_be_consecutive();
  test_rejects_bad_parity();
}
