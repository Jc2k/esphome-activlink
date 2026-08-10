#include "pairing.h"

#include <algorithm>

namespace esphome::honeywell_activlink {

PairingTable::PairingTable() { this->clear(); }

PairResult PairingTable::add(uint32_t device_id, size_t *slot) {
  if (device_id > 0xFFFFF) {
    return PairResult::INVALID;
  }

  const int existing = this->find(device_id);
  if (existing >= 0) {
    if (slot != nullptr) {
      *slot = static_cast<size_t>(existing);
    }
    return PairResult::ALREADY_PAIRED;
  }

  const auto empty = std::find(this->device_ids_.begin(), this->device_ids_.end(), EMPTY_ACTIVLINK_ID);
  if (empty == this->device_ids_.end()) {
    return PairResult::FULL;
  }

  *empty = device_id;
  const size_t index = static_cast<size_t>(empty - this->device_ids_.begin());
  const uint8_t bit = static_cast<uint8_t>(1U << index);
  this->battery_known_mask_ &= static_cast<uint8_t>(~bit);
  this->battery_low_mask_ &= static_cast<uint8_t>(~bit);
  if (slot != nullptr) {
    *slot = index;
  }
  return PairResult::ADDED;
}

bool PairingTable::remove(uint32_t device_id, size_t *slot) {
  const int existing = this->find(device_id);
  if (existing < 0) {
    return false;
  }

  const size_t index = static_cast<size_t>(existing);
  this->device_ids_[index] = EMPTY_ACTIVLINK_ID;
  const uint8_t bit = static_cast<uint8_t>(1U << index);
  this->battery_known_mask_ &= static_cast<uint8_t>(~bit);
  this->battery_low_mask_ &= static_cast<uint8_t>(~bit);
  if (slot != nullptr) {
    *slot = index;
  }
  return true;
}

int PairingTable::find(uint32_t device_id) const {
  const auto existing = std::find(this->device_ids_.begin(), this->device_ids_.end(), device_id);
  return existing == this->device_ids_.end() ? -1 : static_cast<int>(existing - this->device_ids_.begin());
}

bool PairingTable::restore(const std::array<uint32_t, MAX_PAIRED_BUTTONS> &device_ids, uint8_t battery_known_mask,
                           uint8_t battery_low_mask) {
  if ((battery_low_mask & static_cast<uint8_t>(~battery_known_mask)) != 0) {
    return false;
  }

  for (size_t index = 0; index < device_ids.size(); index++) {
    const uint32_t device_id = device_ids[index];
    if (device_id == EMPTY_ACTIVLINK_ID) {
      if ((battery_known_mask & static_cast<uint8_t>(1U << index)) != 0) {
        return false;
      }
      continue;
    }
    if (device_id > 0xFFFFF) {
      return false;
    }
    for (size_t previous = 0; previous < index; previous++) {
      if (device_ids[previous] == device_id) {
        return false;
      }
    }
  }

  this->device_ids_ = device_ids;
  this->battery_known_mask_ = battery_known_mask;
  this->battery_low_mask_ = battery_low_mask;
  return true;
}

void PairingTable::clear() {
  this->device_ids_.fill(EMPTY_ACTIVLINK_ID);
  this->battery_known_mask_ = 0;
  this->battery_low_mask_ = 0;
}

bool PairingTable::update_battery(size_t slot, bool battery_low) {
  if (slot >= MAX_PAIRED_BUTTONS || this->device_ids_[slot] == EMPTY_ACTIVLINK_ID) {
    return false;
  }

  const uint8_t bit = static_cast<uint8_t>(1U << slot);
  const bool changed = (this->battery_known_mask_ & bit) == 0 || this->battery_low(slot) != battery_low;
  this->battery_known_mask_ |= bit;
  if (battery_low) {
    this->battery_low_mask_ |= bit;
  } else {
    this->battery_low_mask_ &= static_cast<uint8_t>(~bit);
  }
  return changed;
}

bool PairingTable::battery_known(size_t slot) const {
  return slot < MAX_PAIRED_BUTTONS &&
         (this->battery_known_mask_ & static_cast<uint8_t>(1U << slot)) != 0;
}

bool PairingTable::battery_low(size_t slot) const {
  return slot < MAX_PAIRED_BUTTONS && (this->battery_low_mask_ & static_cast<uint8_t>(1U << slot)) != 0;
}

size_t PairingTable::size() const {
  return static_cast<size_t>(std::count_if(this->device_ids_.begin(), this->device_ids_.end(),
                                           [](uint32_t device_id) { return device_id != EMPTY_ACTIVLINK_ID; }));
}

}  // namespace esphome::honeywell_activlink
