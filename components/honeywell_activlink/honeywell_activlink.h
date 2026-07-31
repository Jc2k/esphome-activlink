#pragma once

#include "esphome/components/api/api_server.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/event/event.h"
#include "esphome/components/remote_base/remote_base.h"
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
};

}  // namespace esphome::honeywell_activlink
