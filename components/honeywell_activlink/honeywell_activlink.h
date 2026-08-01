#pragma once

#include "esphome/components/api/api_server.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/event/event.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "protocol.h"

#include <cstdint>
#include <vector>

namespace esphome::honeywell_activlink {

class ActivLinkComponent : public Component, public remote_base::RemoteReceiverListener {
 public:
  void add_button(uint32_t device_id, event::Event *press_event,
                  binary_sensor::BinarySensor *battery_low);
  void set_api_encryption_key(api::psk_t key) {
    api_encryption_key_ = key;
    persist_api_encryption_key_ = true;
  }
  void set_deduplication(uint32_t deduplication_ms) { deduplication_ms_ = deduplication_ms; }
  void set_diagnostic_update_interval(uint32_t interval_ms) { diagnostic_update_interval_ms_ = interval_ms; }
  void set_valid_frames_sensor(sensor::Sensor *sensor) { valid_frames_sensor_ = sensor; }
  void set_decode_failures_sensor(sensor::Sensor *sensor) { decode_failures_sensor_ = sensor; }
  void set_duplicate_frames_sensor(sensor::Sensor *sensor) { duplicate_frames_sensor_ = sensor; }
  void set_unconfigured_frames_sensor(sensor::Sensor *sensor) { unconfigured_frames_sensor_ = sensor; }
  void set_last_valid_frame_age_sensor(sensor::Sensor *sensor) { last_valid_frame_age_sensor_ = sensor; }

  void setup() override;
  void loop() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;
  void dump_config() override;

 protected:
  struct Button {
    uint32_t device_id;
    event::Event *press_event;
    binary_sensor::BinarySensor *battery_low;
    uint32_t last_event_ms{0};
    bool last_secret{false};
    bool seen{false};
  };

  std::vector<Button> buttons_{};
  api::psk_t api_encryption_key_{};
  bool persist_api_encryption_key_{false};
  uint32_t deduplication_ms_{250};
  uint32_t diagnostic_update_interval_ms_{60000};
  uint32_t last_diagnostic_publish_ms_{0};
  uint32_t valid_frames_{0};
  uint32_t decode_failures_{0};
  uint32_t duplicate_frames_{0};
  uint32_t unconfigured_frames_{0};
  uint32_t last_valid_frame_ms_{0};
  bool has_valid_frame_{false};
  sensor::Sensor *valid_frames_sensor_{nullptr};
  sensor::Sensor *decode_failures_sensor_{nullptr};
  sensor::Sensor *duplicate_frames_sensor_{nullptr};
  sensor::Sensor *unconfigured_frames_sensor_{nullptr};
  sensor::Sensor *last_valid_frame_age_sensor_{nullptr};

  void publish_diagnostics_(uint32_t now);
};

}  // namespace esphome::honeywell_activlink
