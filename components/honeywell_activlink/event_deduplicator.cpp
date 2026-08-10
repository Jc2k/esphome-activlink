#include "event_deduplicator.h"

namespace esphome::honeywell_activlink {

bool EventDeduplicator::should_publish(uint32_t now, bool secret_press, bool relay,
                                       uint32_t repeat_window_ms, uint32_t relay_window_ms) {
  const auto &state = this->states_[secret_press ? 1 : 0];
  if (state.seen) {
    const uint32_t elapsed = now - state.last_event_ms;
    const bool repeated_frame = elapsed < repeat_window_ms;
    // A direct transmission can be missed while one or more relay-marked
    // copies are decoded. Apply the longer window to every relay frame so
    // those copies do not become separate Home Assistant events.
    const bool relayed_copy = relay && elapsed < relay_window_ms;
    if (repeated_frame || relayed_copy) {
      return false;
    }
  }

  this->record(now, secret_press);
  return true;
}

void EventDeduplicator::record(uint32_t now, bool secret_press) {
  auto &state = this->states_[secret_press ? 1 : 0];
  state.last_event_ms = now;
  state.seen = true;
}

}  // namespace esphome::honeywell_activlink
