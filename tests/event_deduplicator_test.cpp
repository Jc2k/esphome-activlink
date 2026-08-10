#include "components/honeywell_activlink/event_deduplicator.h"

#include <cassert>
#include <cstdint>
#include <limits>

using esphome::honeywell_activlink::EventDeduplicator;

namespace {

constexpr uint32_t REPEAT_WINDOW_MS = 250;
constexpr uint32_t RELAY_WINDOW_MS = 4000;

void test_suppresses_late_relay_copy() {
  EventDeduplicator deduplicator;
  assert(deduplicator.should_publish(1000, false, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(!deduplicator.should_publish(2350, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
}

void test_suppressed_copy_does_not_extend_window() {
  EventDeduplicator deduplicator;
  assert(deduplicator.should_publish(1000, false, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(!deduplicator.should_publish(2300, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  // A real second press is measured from the last published event, not from
  // the suppressed relay copy.
  assert(deduplicator.should_publish(2500, false, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
}

void test_preserves_rapid_real_presses() {
  EventDeduplicator deduplicator;
  assert(deduplicator.should_publish(1000, false, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(!deduplicator.should_publish(1200, false, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(deduplicator.should_publish(1300, false, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
}

void test_secret_press_is_a_distinct_event() {
  EventDeduplicator deduplicator;
  assert(deduplicator.should_publish(1000, false, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(deduplicator.should_publish(1100, true, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  // Interleaving the two event types must not make either relay copy appear
  // to be a new event.
  assert(!deduplicator.should_publish(2300, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(!deduplicator.should_publish(2400, true, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
}

void test_relay_only_reception_remains_usable() {
  EventDeduplicator deduplicator;
  assert(deduplicator.should_publish(1000, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(!deduplicator.should_publish(1100, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(!deduplicator.should_publish(3500, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(deduplicator.should_publish(5100, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
}

void test_millis_wraparound() {
  EventDeduplicator deduplicator;
  constexpr uint32_t near_wrap = std::numeric_limits<uint32_t>::max() - 99;
  assert(deduplicator.should_publish(near_wrap, false, false, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(!deduplicator.should_publish(1200, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
  assert(deduplicator.should_publish(4200, false, true, REPEAT_WINDOW_MS, RELAY_WINDOW_MS));
}

}  // namespace

int main() {
  test_suppresses_late_relay_copy();
  test_suppressed_copy_does_not_extend_window();
  test_preserves_rapid_real_presses();
  test_secret_press_is_a_distinct_event();
  test_relay_only_reception_remains_usable();
  test_millis_wraparound();
}
