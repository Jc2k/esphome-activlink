#pragma once

#include <cstdint>

namespace esphome::honeywell_activlink {

// ActivLink receivers can repeat a button press themselves and can also emit a
// relay-marked copy roughly a second after the original broadcast. Keep the
// two cases separate so the longer relay window does not hide a genuine rapid
// second direct press.
class EventDeduplicator {
 public:
  bool should_publish(uint32_t now, bool secret_press, bool relay, uint32_t repeat_window_ms,
                      uint32_t relay_window_ms);
  void record(uint32_t now, bool secret_press);
  void reset() { states_[0].seen = states_[1].seen = false; }

 protected:
  struct EventState {
    uint32_t last_event_ms{0};
    bool seen{false};
  };

  EventState states_[2]{};
};

}  // namespace esphome::honeywell_activlink
