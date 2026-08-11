#include "components/honeywell_activlink/protocol.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using esphome::honeywell_activlink::ActivLinkFrame;
using esphome::honeywell_activlink::ActivLinkBurstDecoder;
using esphome::honeywell_activlink::activlink_capture_has_demodulator_collapse;
using esphome::honeywell_activlink::activlink_capture_is_plausible;
using esphome::honeywell_activlink::decode_activlink;
using esphome::honeywell_activlink::decode_activlink_rows;
using esphome::honeywell_activlink::summarize_activlink_capture;

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

void test_accumulates_consensus_across_rmt_captures() {
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  const auto one_row = encode(bytes, 1);
  std::vector<ActivLinkFrame> rows;
  assert(decode_activlink_rows(one_row, &rows) == 1);
  assert(rows[0].bytes == bytes);

  ActivLinkBurstDecoder decoder;
  ActivLinkFrame frame{};
  size_t candidate_rows = 0;
  assert(!decoder.decode(one_row, 100, &frame, &candidate_rows));
  assert(candidate_rows == 1);
  assert(!decoder.decode(one_row, 500, &frame));
  assert(!decoder.decode(one_row, 900, &frame));
  assert(decoder.decode(one_row, 1300, &frame));
  assert(frame.bytes == bytes);
  // More chunks from the same RF burst must not emit the same frame twice.
  assert(!decoder.decode(one_row, 1600, &frame));
}

void test_new_burst_can_emit_same_frame_again() {
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  const auto one_row = encode(bytes, 1);
  ActivLinkBurstDecoder decoder;
  ActivLinkFrame frame{};
  assert(!decoder.decode(one_row, 100, &frame));
  assert(!decoder.decode(one_row, 200, &frame));
  assert(!decoder.decode(one_row, 300, &frame));
  assert(decoder.decode(one_row, 400, &frame));

  assert(!decoder.decode(one_row, 2001, &frame));
  assert(!decoder.decode(one_row, 2101, &frame));
  assert(!decoder.decode(one_row, 2201, &frame));
  assert(decoder.decode(one_row, 2301, &frame));
}

void test_rejected_captures_do_not_extend_an_emitted_burst() {
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  const auto four_rows = encode(bytes, 4);
  const std::vector<int32_t> rejected_capture{
      -1841, 60, -1760, 60, -3281, 60, -2560, 60, -761, 60, -1920, 80,
  };
  ActivLinkBurstDecoder decoder;
  ActivLinkFrame frame{};
  size_t candidate_rows = 1;

  assert(decoder.decode(four_rows, 100, &frame));
  assert(!decoder.decode(rejected_capture, 1000, &frame, &candidate_rows));
  assert(candidate_rows == 0);
  assert(!decoder.decode(rejected_capture, 2000, &frame));
  assert(!decoder.decode(rejected_capture, 3000, &frame));
  assert(decoder.decode(four_rows, 4000, &frame));
}

void test_other_frames_do_not_extend_an_emitted_burst() {
  const FrameBytes normal{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  const FrameBytes secret{0xFB, 0x10, 0x00, 0x20, 0x00, 0x10};
  const auto normal_rows = encode(normal, 4);
  const auto one_secret_row = encode(secret, 1);
  ActivLinkBurstDecoder decoder;
  ActivLinkFrame frame{};

  assert(decoder.decode(normal_rows, 100, &frame));
  assert(!decoder.decode(one_secret_row, 1000, &frame));
  assert(!decoder.decode(one_secret_row, 2000, &frame));
  assert(!decoder.decode(one_secret_row, 3000, &frame));
  assert(decoder.decode(normal_rows, 4000, &frame));
  assert(frame.bytes == normal);
}

void test_capture_summary_is_bounded_and_useful() {
  const auto timings = encode(FrameBytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01}, 1);
  const auto summary = summarize_activlink_capture(timings);
  assert(summary.find("n=") == 0);
  assert(summary.find("short=") != std::string::npos);
  assert(summary.find("sync=") != std::string::npos);
  assert(summary.find("plausible=1") != std::string::npos);
  assert(summary.find("collapse=0") != std::string::npos);
  assert(summary.size() < 240);
}

void test_detects_collapsed_fsk_demodulator_output() {
  const std::array<int32_t, 12> pattern{
      60, -3121, 100, -1980, 60, -4302, 60, -1760, 60, -4401, 80, -1400,
  };
  std::vector<int32_t> collapsed;
  for (unsigned repeat = 0; repeat < 8; repeat++) {
    collapsed.insert(collapsed.end(), pattern.begin(), pattern.end());
  }

  assert(activlink_capture_has_demodulator_collapse(collapsed));
  assert(summarize_activlink_capture(collapsed).find("collapse=1") != std::string::npos);
  assert(!activlink_capture_has_demodulator_collapse(
      encode(FrameBytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01}, 1)));
}

void test_filters_protocol_impossible_rf_noise() {
  const std::array<int32_t, 12> noise_pattern{
      60, -820, 80, -3401, 60, -140, 60, -2961, 60, -760, 80, -120,
  };
  std::vector<int32_t> noise;
  for (unsigned repeat = 0; repeat < 12; repeat++) {
    noise.insert(noise.end(), noise_pattern.begin(), noise_pattern.end());
  }

  assert(activlink_capture_has_demodulator_collapse(noise));
  assert(!activlink_capture_is_plausible(noise));
  assert(!activlink_capture_is_plausible(std::vector<int32_t>(82, 60)));
}

void test_filters_dense_rf_with_sparse_coincidental_pairs() {
  // Ambient captures reported in the field contain hundreds of timings and
  // enough short/long symbols to produce isolated matches by chance. Eight
  // scattered valid-looking pairs satisfied the old plausibility score.
  std::vector<int32_t> noise{60, -720, 60, -1401, 140, -1520,
                             60, -1581, 60, -1700, 60, -2301};
  noise.push_back(500);
  for (size_t bit = 0; bit < 48; bit++) {
    if (bit % 6U == 0) {
      noise.push_back(-160);
      noise.push_back(320);
    } else {
      noise.push_back(-800);
      noise.push_back(800);
    }
  }
  noise.push_back(-500);
  noise.resize(422, 800);

  assert(!activlink_capture_is_plausible(noise));
}

void test_accepts_a_damaged_but_plausible_activlink_row() {
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  auto damaged = encode(bytes, 1);
  damaged[2] = 800;

  std::vector<ActivLinkFrame> rows;
  assert(decode_activlink_rows(damaged, &rows) == 0);
  assert(activlink_capture_is_plausible(damaged));
}

void test_accepts_plausible_rows_with_a_collapse_signature() {
  const FrameBytes bytes{0xFB, 0x10, 0x00, 0x20, 0x00, 0x01};
  auto collapsed = encode(bytes, 1);
  // Preserve eighteen correctly aligned PWM pairs while replacing thirty
  // pairs with the polarity-specific glitch/gap collapse signature.
  for (size_t bit = 0; bit < 30; bit++) {
    const size_t cursor = 2U + bit * 2U;
    collapsed[cursor] = 60;
    collapsed[cursor + 1U] = -1000;
  }

  assert(activlink_capture_has_demodulator_collapse(collapsed));
  assert(activlink_capture_is_plausible(collapsed));
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
  test_accumulates_consensus_across_rmt_captures();
  test_new_burst_can_emit_same_frame_again();
  test_rejected_captures_do_not_extend_an_emitted_burst();
  test_other_frames_do_not_extend_an_emitted_burst();
  test_capture_summary_is_bounded_and_useful();
  test_detects_collapsed_fsk_demodulator_output();
  test_filters_protocol_impossible_rf_noise();
  test_filters_dense_rf_with_sparse_coincidental_pairs();
  test_accepts_a_damaged_but_plausible_activlink_row();
  test_accepts_plausible_rows_with_a_collapse_signature();
  test_matching_repetitions_need_not_be_consecutive();
  test_rejects_bad_parity();
}
