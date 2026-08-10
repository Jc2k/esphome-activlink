#pragma once

#include "esphome/components/api/api_server.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/cc1101/cc1101.h"
#include "esphome/components/event/event.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "event_deduplicator.h"
#include "pairing.h"
#include "protocol.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::honeywell_activlink {

class ActivLinkComponent;

class ActivLinkPairButton : public button::Button {
 public:
  void set_parent(ActivLinkComponent *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;
  ActivLinkComponent *parent_{nullptr};
};

class ActivLinkRemoveButton : public button::Button {
 public:
  void set_parent(ActivLinkComponent *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;
  ActivLinkComponent *parent_{nullptr};
};

class ActivLinkCancelButton : public button::Button {
 public:
  void set_parent(ActivLinkComponent *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;
  ActivLinkComponent *parent_{nullptr};
};

class ActivLinkForgetButton : public button::Button {
 public:
  void set_parent(ActivLinkComponent *parent) { this->parent_ = parent; }
  void set_slot(size_t slot) { this->slot_ = slot; }

 protected:
  void press_action() override;
  ActivLinkComponent *parent_{nullptr};
  size_t slot_{0};
};

class ActivLinkComponent : public Component, public remote_base::RemoteReceiverListener {
 public:
  void add_slot(event::Event *press_event, binary_sensor::BinarySensor *battery_low,
                binary_sensor::BinarySensor *paired, text_sensor::TextSensor *transmitter_id,
                button::Button *forget_button);
  void set_api_encryption_key(api::psk_t key) {
    api_encryption_key_ = key;
    persist_api_encryption_key_ = true;
  }
  void set_deduplication(uint32_t deduplication_ms) { deduplication_ms_ = deduplication_ms; }
  void set_relay_deduplication(uint32_t relay_deduplication_ms) {
    relay_deduplication_ms_ = relay_deduplication_ms;
  }
  void set_pairing_timeout(uint32_t timeout_ms) { pairing_timeout_ms_ = timeout_ms; }
  void set_diagnostic_update_interval(uint32_t interval_ms) { diagnostic_update_interval_ms_ = interval_ms; }
  void set_radio(cc1101::CC1101Component *radio) { radio_ = radio; }
  void set_pairing_state_text_sensor(text_sensor::TextSensor *sensor) { pairing_state_text_sensor_ = sensor; }
  void set_paired_count_sensor(sensor::Sensor *sensor) { paired_count_sensor_ = sensor; }
  void set_valid_frames_sensor(sensor::Sensor *sensor) { valid_frames_sensor_ = sensor; }
  void set_decode_failures_sensor(sensor::Sensor *sensor) { decode_failures_sensor_ = sensor; }
  void set_candidate_rows_sensor(sensor::Sensor *sensor) { candidate_rows_sensor_ = sensor; }
  void set_duplicate_frames_sensor(sensor::Sensor *sensor) { duplicate_frames_sensor_ = sensor; }
  void set_unconfigured_frames_sensor(sensor::Sensor *sensor) { unconfigured_frames_sensor_ = sensor; }
  void set_last_valid_frame_age_sensor(sensor::Sensor *sensor) { last_valid_frame_age_sensor_ = sensor; }
  void set_last_capture_summary_text_sensor(text_sensor::TextSensor *sensor) {
    last_capture_summary_text_sensor_ = sensor;
  }
  void set_radio_recoveries_sensor(sensor::Sensor *sensor) { radio_recoveries_sensor_ = sensor; }

  void setup() override;
  void loop() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;
  void dump_config() override;

  void start_pairing();
  void start_removal();
  void cancel_pairing();
  void forget_slot(size_t slot);

 protected:
  enum class PairingMode : uint8_t {
    IDLE,
    PAIR,
    REMOVE,
  };

  struct Slot {
    event::Event *press_event;
    binary_sensor::BinarySensor *battery_low;
    binary_sensor::BinarySensor *paired;
    text_sensor::TextSensor *transmitter_id;
    button::Button *forget_button;
    EventDeduplicator event_deduplicator;
  };

  struct StoredPairingsV1 {
    uint32_t magic;
    uint32_t version;
    std::array<uint32_t, MAX_PAIRED_BUTTONS> device_ids;
  };

  struct StoredPairings {
    uint32_t magic;
    uint32_t version;
    std::array<uint32_t, MAX_PAIRED_BUTTONS> device_ids;
    uint8_t battery_known_mask;
    uint8_t battery_low_mask;
    uint16_t reserved;
  };

  static_assert(sizeof(StoredPairingsV1) == 40, "legacy pairing storage layout changed");
  static_assert(sizeof(StoredPairings) == 44, "pairing storage layout changed");

  static constexpr uint32_t RADIO_RECOVERY_COOLDOWN_MS = 5000;

  std::vector<Slot> slots_{};
  PairingTable pairings_{};
  ActivLinkBurstDecoder burst_decoder_{};
  cc1101::CC1101Component *radio_{nullptr};
  ESPPreferenceObject pairing_preference_{};
  api::psk_t api_encryption_key_{};
  bool persist_api_encryption_key_{false};
  PairingMode pairing_mode_{PairingMode::IDLE};
  uint32_t pairing_deadline_ms_{0};
  uint32_t deduplication_ms_{250};
  uint32_t relay_deduplication_ms_{4000};
  uint32_t pairing_timeout_ms_{30000};
  uint32_t diagnostic_update_interval_ms_{60000};
  uint32_t last_diagnostic_publish_ms_{0};
  uint32_t valid_frames_{0};
  uint32_t decode_failures_{0};
  uint32_t candidate_rows_{0};
  uint32_t duplicate_frames_{0};
  uint32_t unconfigured_frames_{0};
  uint32_t radio_recoveries_{0};
  uint32_t last_radio_recovery_ms_{0};
  uint32_t last_valid_frame_ms_{0};
  bool has_valid_frame_{false};
  bool discovery_refresh_pending_{false};
  bool radio_recovery_pending_{false};
  bool has_radio_recovery_{false};
  sensor::Sensor *valid_frames_sensor_{nullptr};
  sensor::Sensor *decode_failures_sensor_{nullptr};
  sensor::Sensor *candidate_rows_sensor_{nullptr};
  sensor::Sensor *duplicate_frames_sensor_{nullptr};
  sensor::Sensor *unconfigured_frames_sensor_{nullptr};
  sensor::Sensor *radio_recoveries_sensor_{nullptr};
  sensor::Sensor *last_valid_frame_age_sensor_{nullptr};
  sensor::Sensor *paired_count_sensor_{nullptr};
  text_sensor::TextSensor *pairing_state_text_sensor_{nullptr};
  text_sensor::TextSensor *last_capture_summary_text_sensor_{nullptr};
  std::string last_capture_summary_{};

  bool load_pairings_();
  bool save_pairings_();
  void remember_battery_state_(size_t slot, bool battery_low);
  bool handle_pairing_frame_(uint32_t device_id, bool secret_press, bool battery_low);
  void publish_pairing_state_(const std::string &state);
  void configure_slot_discovery_();
  void schedule_discovery_refresh_();
  void publish_slot_state_(size_t slot);
  void publish_all_slot_states_();
  void publish_diagnostics_(uint32_t now);
};

}  // namespace esphome::honeywell_activlink
