#include "honeywell_activlink.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome::honeywell_activlink {

static const char *const TAG = "honeywell_activlink";

void ActivLinkComponent::add_button(uint32_t device_id, event::Event *press_event,
                                    binary_sensor::BinarySensor *battery_low) {
  this->buttons_.push_back({device_id, press_event, battery_low});
}

void ActivLinkComponent::loop() {
  if (!this->persist_api_encryption_key_) {
    return;
  }
  this->persist_api_encryption_key_ = false;

  // Public OTA images omit secrets and let the API load its key from flash.
  // Save the locally configured key once so that transition is seamless.
  if (api::global_api_server == nullptr ||
      !api::global_api_server->save_noise_psk(this->api_encryption_key_, false)) {
    ESP_LOGE(TAG, "Failed to persist API encryption key for managed updates");
    return;
  }
  api::global_api_server->set_noise_psk(this->api_encryption_key_);
}

bool ActivLinkComponent::on_receive(remote_base::RemoteReceiveData data) {
  ActivLinkFrame frame{};
  if (!decode_activlink(data.get_raw_data(), &frame)) {
    return false;
  }

  const uint32_t device_id = frame.device_id();
  ESP_LOGD(TAG,
           "ActivLink id=0x%05" PRIX32 " type=%u secret=%s relay=%s battery=%s frame=%02X%02X%02X%02X%02X%02X",
           device_id, frame.device_type(), YESNO(frame.secret_press()), YESNO(frame.relay()),
           frame.battery_low() ? "LOW" : "OK", frame.bytes[0], frame.bytes[1], frame.bytes[2], frame.bytes[3],
           frame.bytes[4], frame.bytes[5]);

  if (frame.device_type() != 0x02) {
    ESP_LOGD(TAG, "Ignoring non-doorbell ActivLink device 0x%05" PRIX32 " (type %u)", device_id,
             frame.device_type());
    return true;
  }

  for (auto &button : this->buttons_) {
    if (button.device_id != device_id) {
      continue;
    }

    button.battery_low->publish_state(frame.battery_low());

    const uint32_t now = millis();
    const bool duplicate = button.seen && button.last_secret == frame.secret_press() &&
                           now - button.last_event_ms < this->deduplication_ms_;
    button.last_event_ms = now;
    button.last_secret = frame.secret_press();
    button.seen = true;

    if (!duplicate) {
      button.press_event->trigger(frame.secret_press() ? "secret_press" : "press");
    }
    return true;
  }

  ESP_LOGW(TAG,
           "Unconfigured ActivLink doorbell id=0x%05" PRIX32
           "; add activlink_id: 0x%05" PRIX32 " to honeywell_activlink.buttons",
           device_id, device_id);
  return true;
}

void ActivLinkComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Honeywell ActivLink decoder:");
  ESP_LOGCONFIG(TAG, "  Deduplication: %" PRIu32 " ms", this->deduplication_ms_);
  for (const auto &button : this->buttons_) {
    ESP_LOGCONFIG(TAG, "  Doorbell ID: 0x%05" PRIX32, button.device_id);
  }
}

}  // namespace esphome::honeywell_activlink
