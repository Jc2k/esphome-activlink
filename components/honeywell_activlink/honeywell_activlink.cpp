#include "honeywell_activlink.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>
#include <cstdio>
#include <limits>

namespace esphome::honeywell_activlink {

static const char *const TAG = "honeywell_activlink";
static constexpr uint32_t PAIRING_PREFERENCE_KEY = 0xA11C7101;
static constexpr uint32_t PAIRING_PREFERENCE_MAGIC = 0x414C5042;
static constexpr uint32_t PAIRING_PREFERENCE_VERSION = 2;
static constexpr uint32_t LEGACY_PAIRING_PREFERENCE_VERSION = 1;

void ActivLinkPairButton::press_action() {
  if (this->parent_ != nullptr) {
    this->parent_->start_pairing();
  }
}

void ActivLinkRemoveButton::press_action() {
  if (this->parent_ != nullptr) {
    this->parent_->start_removal();
  }
}

void ActivLinkCancelButton::press_action() {
  if (this->parent_ != nullptr) {
    this->parent_->cancel_pairing();
  }
}

void ActivLinkForgetButton::press_action() {
  if (this->parent_ != nullptr) {
    this->parent_->forget_slot(this->slot_);
  }
}

void ActivLinkComponent::add_slot(event::Event *press_event, binary_sensor::BinarySensor *battery_low,
                                  binary_sensor::BinarySensor *paired, text_sensor::TextSensor *transmitter_id,
                                  button::Button *forget_button) {
  this->slots_.push_back({press_event, battery_low, paired, transmitter_id, forget_button});
}

void ActivLinkComponent::setup() {
  if (this->slots_.size() != MAX_PAIRED_BUTTONS) {
    ESP_LOGE(TAG, "Expected %u runtime pairing slots, got %u", static_cast<unsigned>(MAX_PAIRED_BUTTONS),
             static_cast<unsigned>(this->slots_.size()));
    this->mark_failed();
    return;
  }

  this->pairing_preference_ =
      global_preferences->make_preference<StoredPairings>(PAIRING_PREFERENCE_KEY, true);
  this->load_pairings_();
  this->configure_slot_discovery_();
  this->publish_all_slot_states_();
  this->publish_pairing_state_("Idle");
  this->last_diagnostic_publish_ms_ = millis() - this->diagnostic_update_interval_ms_;
}

void ActivLinkComponent::loop() {
  if (this->persist_api_encryption_key_) {
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

  const uint32_t now = millis();
  if (this->radio_recovery_pending_) {
    this->radio_recovery_pending_ = false;
    this->has_radio_recovery_ = true;
    this->last_radio_recovery_ms_ = now;
    this->radio_recoveries_++;
    ESP_LOGW(TAG, "Restarting CC1101 RX after collapsed FSK capture");
    this->radio_->begin_rx();
  }

  if (this->pairing_mode_ != PairingMode::IDLE &&
      static_cast<int32_t>(now - this->pairing_deadline_ms_) >= 0) {
    this->pairing_mode_ = PairingMode::IDLE;
    this->publish_pairing_state_("Timed out");
    ESP_LOGI(TAG, "ActivLink pairing mode timed out");
  }

  if (now - this->last_diagnostic_publish_ms_ >= this->diagnostic_update_interval_ms_) {
    this->last_diagnostic_publish_ms_ = now;
    this->publish_diagnostics_(now);
  }
}

bool ActivLinkComponent::on_receive(remote_base::RemoteReceiveData data) {
  ActivLinkFrame frame{};
  size_t candidate_rows = 0;
  const auto &raw_data = data.get_raw_data();
  const uint32_t now = millis();
  if (!this->burst_decoder_.decode(raw_data, now, &frame, &candidate_rows)) {
    this->candidate_rows_ += candidate_rows;
    const bool plausible_capture = candidate_rows != 0 || activlink_capture_is_plausible(raw_data);
    if (!plausible_capture) {
      return false;
    }

    this->decode_failures_++;
    if (candidate_rows == 0 && raw_data.size() >= 80) {
      this->last_capture_summary_ = summarize_activlink_capture(raw_data);
    }
    if (candidate_rows == 0 && activlink_capture_has_demodulator_collapse(raw_data) &&
        !this->radio_recovery_pending_ &&
        (!this->has_radio_recovery_ || now - this->last_radio_recovery_ms_ >= RADIO_RECOVERY_COOLDOWN_MS)) {
      this->radio_recovery_pending_ = true;
    }
    return false;
  }

  this->candidate_rows_ += candidate_rows;
  this->valid_frames_++;
  this->last_valid_frame_ms_ = now;
  this->has_valid_frame_ = true;

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

  if (this->pairing_mode_ != PairingMode::IDLE) {
    this->handle_pairing_frame_(device_id, frame.secret_press(), frame.battery_low());
    return true;
  }

  const int slot_index = this->pairings_.find(device_id);
  if (slot_index >= 0) {
    const size_t pairing_slot = static_cast<size_t>(slot_index);
    auto &slot = this->slots_[pairing_slot];
    this->remember_battery_state_(pairing_slot, frame.battery_low());
    slot.battery_low->publish_state(frame.battery_low());

    const uint32_t now = millis();
    if (slot.event_deduplicator.should_publish(now, frame.secret_press(), frame.relay(),
                                               this->deduplication_ms_, this->relay_deduplication_ms_)) {
      const char *event_type = frame.secret_press() ? "secret_press" : "ring";
      ESP_LOGD(TAG, "Publishing %s event for ActivLink id=0x%05" PRIX32, event_type, device_id);
      slot.press_event->trigger(event_type);
    } else {
      this->duplicate_frames_++;
      ESP_LOGD(TAG, "Suppressing duplicate%s ActivLink frame for id=0x%05" PRIX32,
               frame.relay() ? " relay" : "", device_id);
    }
    return true;
  }

  this->unconfigured_frames_++;
  ESP_LOGW(TAG, "Unpaired ActivLink doorbell id=0x%05" PRIX32 "; start pairing and press it again", device_id);
  return true;
}

void ActivLinkComponent::start_pairing() {
  if (this->pairings_.full()) {
    this->pairing_mode_ = PairingMode::IDLE;
    this->publish_pairing_state_("Pairing unavailable: all slots are occupied");
    ESP_LOGW(TAG, "Cannot pair another ActivLink button: all %u slots are occupied",
             static_cast<unsigned>(MAX_PAIRED_BUTTONS));
    return;
  }

  this->pairing_mode_ = PairingMode::PAIR;
  this->pairing_deadline_ms_ = millis() + this->pairing_timeout_ms_;
  this->publish_pairing_state_("Pairing: press an ActivLink button");
  ESP_LOGI(TAG, "Pairing enabled for %" PRIu32 " seconds; press an ActivLink button",
           this->pairing_timeout_ms_ / 1000);
}

void ActivLinkComponent::start_removal() {
  if (this->pairings_.size() == 0) {
    this->pairing_mode_ = PairingMode::IDLE;
    this->publish_pairing_state_("Removal unavailable: no buttons are paired");
    ESP_LOGW(TAG, "Cannot remove an ActivLink button: no buttons are paired");
    return;
  }

  this->pairing_mode_ = PairingMode::REMOVE;
  this->pairing_deadline_ms_ = millis() + this->pairing_timeout_ms_;
  this->publish_pairing_state_("Removal: press the paired button to remove");
  ESP_LOGI(TAG, "Removal enabled for %" PRIu32 " seconds; press the paired button to remove",
           this->pairing_timeout_ms_ / 1000);
}

void ActivLinkComponent::cancel_pairing() {
  this->pairing_mode_ = PairingMode::IDLE;
  this->publish_pairing_state_("Idle (cancelled)");
  ESP_LOGI(TAG, "ActivLink pairing mode cancelled");
}

void ActivLinkComponent::forget_slot(size_t slot) {
  this->pairing_mode_ = PairingMode::IDLE;
  if (slot >= this->slots_.size()) {
    this->publish_pairing_state_("Forget failed: invalid slot");
    ESP_LOGE(TAG, "Cannot forget invalid ActivLink slot %u", static_cast<unsigned>(slot + 1));
    return;
  }

  const uint32_t device_id = this->pairings_.at(slot);
  if (device_id == EMPTY_ACTIVLINK_ID) {
    char status[64];
    snprintf(status, sizeof(status), "Button %u is already unpaired", static_cast<unsigned>(slot + 1));
    this->publish_pairing_state_(status);
    ESP_LOGI(TAG, "ActivLink slot %u is already unpaired", static_cast<unsigned>(slot + 1));
    return;
  }

  const PairingTable previous = this->pairings_;
  if (!this->pairings_.remove(device_id, nullptr) || !this->save_pairings_()) {
    this->pairings_ = previous;
    this->publish_pairing_state_("Forget failed: could not save");
    return;
  }

  this->slots_[slot].event_deduplicator.reset();
  this->publish_slot_state_(slot);
  if (this->paired_count_sensor_ != nullptr) {
    this->paired_count_sensor_->publish_state(this->pairings_.size());
  }

  char status[80];
  snprintf(status, sizeof(status), "Forgot Button %u (0x%05" PRIX32 ")", static_cast<unsigned>(slot + 1),
           device_id);
  this->publish_pairing_state_(status);
  ESP_LOGI(TAG, "Forgot ActivLink id=0x%05" PRIX32 " from slot %u", device_id,
           static_cast<unsigned>(slot + 1));
  this->schedule_discovery_refresh_();
}

bool ActivLinkComponent::load_pairings_() {
  StoredPairings stored{};
  if (this->pairing_preference_.load(&stored)) {
    if (stored.magic != PAIRING_PREFERENCE_MAGIC || stored.version != PAIRING_PREFERENCE_VERSION ||
        !this->pairings_.restore(stored.device_ids, stored.battery_known_mask, stored.battery_low_mask)) {
      ESP_LOGE(TAG, "Saved ActivLink pairings are invalid; ignoring them");
      this->pairings_.clear();
      return false;
    }

    ESP_LOGI(TAG, "Loaded %u saved ActivLink pairing(s)", static_cast<unsigned>(this->pairings_.size()));
    return true;
  }

  // Version 1 stored only transmitter IDs. Load it once, leaving battery
  // states unknown, then replace it with the current record format.
  StoredPairingsV1 legacy{};
  auto legacy_preference =
      global_preferences->make_preference<StoredPairingsV1>(PAIRING_PREFERENCE_KEY, true);
  if (!legacy_preference.load(&legacy)) {
    ESP_LOGI(TAG, "No saved ActivLink pairings; starting empty");
    this->pairings_.clear();
    return false;
  }

  if (legacy.magic != PAIRING_PREFERENCE_MAGIC || legacy.version != LEGACY_PAIRING_PREFERENCE_VERSION ||
      !this->pairings_.restore(legacy.device_ids)) {
    ESP_LOGE(TAG, "Saved legacy ActivLink pairings are invalid; ignoring them");
    this->pairings_.clear();
    return false;
  }

  if (this->save_pairings_()) {
    ESP_LOGI(TAG, "Migrated %u saved ActivLink pairing(s); battery states will be learned on the next press",
             static_cast<unsigned>(this->pairings_.size()));
  } else {
    ESP_LOGW(TAG, "Loaded legacy ActivLink pairings but could not migrate their storage yet");
  }
  return true;
}

bool ActivLinkComponent::save_pairings_() {
  const StoredPairings stored{PAIRING_PREFERENCE_MAGIC, PAIRING_PREFERENCE_VERSION,
                              this->pairings_.device_ids(), this->pairings_.battery_known_mask(),
                              this->pairings_.battery_low_mask(), 0};
  if (!this->pairing_preference_.save(&stored) || !global_preferences->sync()) {
    ESP_LOGE(TAG, "Failed to save ActivLink pairings");
    return false;
  }
  return true;
}

void ActivLinkComponent::remember_battery_state_(size_t slot, bool battery_low) {
  const PairingTable previous = this->pairings_;
  if (!this->pairings_.update_battery(slot, battery_low)) {
    return;
  }
  if (!this->save_pairings_()) {
    // Retry on the next received frame instead of treating a failed write as
    // durable. The live entity is still updated by the caller.
    this->pairings_ = previous;
  }
}

bool ActivLinkComponent::handle_pairing_frame_(uint32_t device_id, bool secret_press, bool battery_low) {
  const PairingTable previous = this->pairings_;
  const uint32_t now = millis();
  size_t slot = 0;

  if (this->pairing_mode_ == PairingMode::PAIR) {
    const PairResult result = this->pairings_.add(device_id, &slot);
    if (result == PairResult::ALREADY_PAIRED) {
      char status[80];
      snprintf(status, sizeof(status), "Already paired: Button %u (0x%05" PRIX32 ")",
               static_cast<unsigned>(slot + 1), device_id);
      this->pairing_mode_ = PairingMode::IDLE;
      this->publish_pairing_state_(status);
      this->slots_[slot].event_deduplicator.record(now, secret_press);
      this->remember_battery_state_(slot, battery_low);
      this->slots_[slot].battery_low->publish_state(battery_low);
      ESP_LOGI(TAG, "ActivLink id=0x%05" PRIX32 " is already paired in slot %u", device_id,
               static_cast<unsigned>(slot + 1));
      return true;
    }
    if (result != PairResult::ADDED) {
      this->pairing_mode_ = PairingMode::IDLE;
      this->publish_pairing_state_("Pairing failed");
      return false;
    }

    this->pairings_.update_battery(slot, battery_low);
    if (!this->save_pairings_()) {
      this->pairings_ = previous;
      this->pairing_mode_ = PairingMode::IDLE;
      this->publish_pairing_state_("Pairing failed: could not save");
      return false;
    }

    // Suppress any second decode from this same physical broadcast after
    // pairing mode has returned to idle.
    this->slots_[slot].event_deduplicator.record(now, secret_press);
    this->publish_slot_state_(slot);
    if (this->paired_count_sensor_ != nullptr) {
      this->paired_count_sensor_->publish_state(this->pairings_.size());
    }
    char status[80];
    snprintf(status, sizeof(status), "Paired Button %u (0x%05" PRIX32 ")", static_cast<unsigned>(slot + 1),
             device_id);
    this->pairing_mode_ = PairingMode::IDLE;
    this->publish_pairing_state_(status);
    ESP_LOGI(TAG, "Paired ActivLink id=0x%05" PRIX32 " in slot %u", device_id,
             static_cast<unsigned>(slot + 1));
    this->schedule_discovery_refresh_();
    return true;
  }

  if (!this->pairings_.remove(device_id, &slot)) {
    char status[96];
    snprintf(status, sizeof(status), "Not paired: 0x%05" PRIX32 "; still waiting for a paired button", device_id);
    this->publish_pairing_state_(status);
    ESP_LOGW(TAG, "Cannot remove unpaired ActivLink id=0x%05" PRIX32 "; removal mode remains active", device_id);
    return false;
  }

  if (!this->save_pairings_()) {
    this->pairings_ = previous;
    this->pairing_mode_ = PairingMode::IDLE;
    this->publish_pairing_state_("Removal failed: could not save");
    return false;
  }

  this->slots_[slot].event_deduplicator.reset();
  this->publish_slot_state_(slot);
  if (this->paired_count_sensor_ != nullptr) {
    this->paired_count_sensor_->publish_state(this->pairings_.size());
  }
  char status[80];
  snprintf(status, sizeof(status), "Removed Button %u (0x%05" PRIX32 ")", static_cast<unsigned>(slot + 1),
           device_id);
  this->pairing_mode_ = PairingMode::IDLE;
  this->publish_pairing_state_(status);
  ESP_LOGI(TAG, "Removed ActivLink id=0x%05" PRIX32 " from slot %u", device_id,
           static_cast<unsigned>(slot + 1));
  this->schedule_discovery_refresh_();
  return true;
}

void ActivLinkComponent::publish_pairing_state_(const std::string &state) {
  if (this->pairing_state_text_sensor_ != nullptr) {
    this->pairing_state_text_sensor_->publish_state(state);
  }
}

void ActivLinkComponent::configure_slot_discovery_() {
#ifdef USE_DEVICES
  StaticVector<Device *, ESPHOME_DEVICE_COUNT> visible_devices;

  for (auto *device : App.get_devices()) {
    bool is_slot_device = false;
    bool is_paired_slot = false;
    for (size_t slot_index = 0; slot_index < this->slots_.size(); slot_index++) {
      const auto &slot = this->slots_[slot_index];
      if (slot.press_event->get_device() == device) {
        is_slot_device = true;
        is_paired_slot = this->pairings_.at(slot_index) != EMPTY_ACTIVLINK_ID;
        break;
      }
    }
    if (!is_slot_device || is_paired_slot) {
      visible_devices.push_back(device);
    }
  }

  auto &registered_devices =
      const_cast<StaticVector<Device *, ESPHOME_DEVICE_COUNT> &>(App.get_devices());
  registered_devices.assign(visible_devices.begin(), visible_devices.end());
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  for (size_t slot_index = 0; slot_index < this->slots_.size(); slot_index++) {
    auto &slot = this->slots_[slot_index];
    const bool internal = this->pairings_.at(slot_index) == EMPTY_ACTIVLINK_ID;
    slot.press_event->set_internal(internal);
    slot.battery_low->set_internal(internal);
    slot.paired->set_internal(internal);
    slot.transmitter_id->set_internal(internal);
    slot.forget_button->set_internal(internal);
  }
#pragma GCC diagnostic pop

  ESP_LOGI(TAG, "Advertising %u paired ActivLink sub-device(s)",
           static_cast<unsigned>(this->pairings_.size()));
}

void ActivLinkComponent::schedule_discovery_refresh_() {
  if (this->discovery_refresh_pending_) {
    return;
  }
  this->discovery_refresh_pending_ = true;
  ESP_LOGI(TAG, "Pairing table changed; restarting to refresh Home Assistant discovery");
  this->set_timeout("activlink_discovery_refresh", 750, []() { App.safe_reboot(); });
}

void ActivLinkComponent::publish_slot_state_(size_t slot_index) {
  auto &slot = this->slots_[slot_index];
  const uint32_t device_id = this->pairings_.at(slot_index);
  const bool paired = device_id != EMPTY_ACTIVLINK_ID;
  slot.paired->publish_state(paired);

  if (!paired) {
    slot.transmitter_id->publish_state("Unpaired");
    slot.battery_low->invalidate_state();
    return;
  }

  char transmitter_id[11];
  snprintf(transmitter_id, sizeof(transmitter_id), "0x%05" PRIX32, device_id);
  slot.transmitter_id->publish_state(transmitter_id);
  if (this->pairings_.battery_known(slot_index)) {
    slot.battery_low->publish_state(this->pairings_.battery_low(slot_index));
  } else {
    slot.battery_low->invalidate_state();
  }
}

void ActivLinkComponent::publish_all_slot_states_() {
  for (size_t slot = 0; slot < this->slots_.size(); slot++) {
    this->publish_slot_state_(slot);
  }
  if (this->paired_count_sensor_ != nullptr) {
    this->paired_count_sensor_->publish_state(this->pairings_.size());
  }
}

void ActivLinkComponent::publish_diagnostics_(uint32_t now) {
  if (this->valid_frames_sensor_ != nullptr) {
    this->valid_frames_sensor_->publish_state(this->valid_frames_);
  }
  if (this->decode_failures_sensor_ != nullptr) {
    this->decode_failures_sensor_->publish_state(this->decode_failures_);
  }
  if (this->candidate_rows_sensor_ != nullptr) {
    this->candidate_rows_sensor_->publish_state(this->candidate_rows_);
  }
  if (this->duplicate_frames_sensor_ != nullptr) {
    this->duplicate_frames_sensor_->publish_state(this->duplicate_frames_);
  }
  if (this->unconfigured_frames_sensor_ != nullptr) {
    this->unconfigured_frames_sensor_->publish_state(this->unconfigured_frames_);
  }
  if (this->radio_recoveries_sensor_ != nullptr) {
    this->radio_recoveries_sensor_->publish_state(this->radio_recoveries_);
  }
  if (this->last_valid_frame_age_sensor_ != nullptr) {
    const float age = this->has_valid_frame_ ? (now - this->last_valid_frame_ms_) / 1000.0f
                                             : std::numeric_limits<float>::quiet_NaN();
    this->last_valid_frame_age_sensor_->publish_state(age);
  }
  if (this->last_capture_summary_text_sensor_ != nullptr && !this->last_capture_summary_.empty()) {
    this->last_capture_summary_text_sensor_->publish_state(this->last_capture_summary_);
  }
}

void ActivLinkComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Honeywell ActivLink decoder:");
  ESP_LOGCONFIG(TAG, "  Deduplication: %" PRIu32 " ms", this->deduplication_ms_);
  ESP_LOGCONFIG(TAG, "  Relay deduplication: %" PRIu32 " ms", this->relay_deduplication_ms_);
  ESP_LOGCONFIG(TAG, "  Pairing timeout: %" PRIu32 " ms", this->pairing_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Diagnostic update interval: %" PRIu32 " ms", this->diagnostic_update_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Collapsed-capture RX recovery: enabled");
  for (size_t slot = 0; slot < this->slots_.size(); slot++) {
    const uint32_t device_id = this->pairings_.at(slot);
    if (device_id == EMPTY_ACTIVLINK_ID) {
      ESP_LOGCONFIG(TAG, "  Button %u: unpaired", static_cast<unsigned>(slot + 1));
    } else {
      ESP_LOGCONFIG(TAG, "  Button %u: 0x%05" PRIX32, static_cast<unsigned>(slot + 1), device_id);
    }
  }
}

}  // namespace esphome::honeywell_activlink
