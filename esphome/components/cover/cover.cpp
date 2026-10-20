#include "cover.h"
#include "esphome/core/defines.h"
#include "esphome/core/controller_registry.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

#include <cmath>
#include <cstring>
#include <strings.h>

namespace esphome::cover {

ESPHOME_LOG_TAG(TAG, "cover");

const LogString *cover_command_to_str(float pos) {
  if (pos == COVER_OPEN) {
    return LOG_STR("OPEN");
  } else if (pos == COVER_CLOSED) {
    return LOG_STR("CLOSE");
  } else {
    return LOG_STR("UNKNOWN");
  }
}
// Cover operation strings indexed by CoverOperation enum (0-2): IDLE, OPENING, CLOSING, plus UNKNOWN
PROGMEM_STRING_TABLE(CoverOperationStrings, "IDLE", "OPENING", "CLOSING", "UNKNOWN");

const LogString *cover_operation_to_str(CoverOperation op) {
  return CoverOperationStrings::get_log_str(static_cast<uint8_t>(op), CoverOperationStrings::LAST_INDEX);
}

Cover::Cover() : position{COVER_OPEN} {}

CoverCall::CoverCall(Cover *parent) : parent_(parent) {}
CoverCall &CoverCall::set_command(const char *command) {
  if (command == nullptr) {
#ifdef USE_COVER_AUTHORIZATION
    this->malformed_request_ = true;
#endif
    return *this;
  }
  if (ESPHOME_strcasecmp_P(command, ESPHOME_PSTR("OPEN")) == 0) {
    this->set_command_open();
  } else if (ESPHOME_strcasecmp_P(command, ESPHOME_PSTR("CLOSE")) == 0) {
    this->set_command_close();
  } else if (ESPHOME_strcasecmp_P(command, ESPHOME_PSTR("STOP")) == 0) {
    this->set_command_stop();
  } else if (ESPHOME_strcasecmp_P(command, ESPHOME_PSTR("TOGGLE")) == 0) {
    this->set_command_toggle();
  } else {
#ifdef USE_COVER_AUTHORIZATION
    this->malformed_request_ = true;
#endif
    ESP_LOGW(TAG, "'%s' - Unrecognized command %s", this->parent_->get_name().c_str(), command);
  }
  return *this;
}
CoverCall &CoverCall::set_command_open() {
  this->position_ = COVER_OPEN;
  return *this;
}
CoverCall &CoverCall::set_command_close() {
  this->position_ = COVER_CLOSED;
  return *this;
}
CoverCall &CoverCall::set_command_stop() {
  this->stop_ = true;
  return *this;
}
CoverCall &CoverCall::set_command_toggle() {
  this->toggle_ = true;
  return *this;
}
CoverCall &CoverCall::set_position(float position) {
  this->position_ = position;
  return *this;
}
CoverCall &CoverCall::set_tilt(float tilt) {
  this->tilt_ = tilt;
  return *this;
}
CoverCall &CoverCall::set_authorization_code(StringRef code) {
#ifdef USE_COVER_AUTHORIZATION
  this->authorization_code_ = code;
#else
  (void) code;
#endif
  return *this;
}
void CoverCall::perform() {
  ESP_LOGV(TAG, "'%s' - Setting", this->parent_->get_name().c_str());
  auto traits = this->parent_->get_traits();
#ifdef USE_COVER_AUTHORIZATION
  const bool conflicting_request =
      (this->stop_ && (this->position_.has_value() || this->tilt_.has_value() || this->toggle_.has_value())) ||
      (this->toggle_.has_value() && (this->position_.has_value() || this->tilt_.has_value()));
  const bool empty_request =
      !this->stop_ && !this->position_.has_value() && !this->tilt_.has_value() && !this->toggle_.has_value();
  bool invalid_request = this->malformed_request_;
  if (this->position_.has_value()) {
    const float position = *this->position_;
    invalid_request |= !std::isfinite(position) || position < COVER_CLOSED || position > COVER_OPEN;
  }
  if (this->tilt_.has_value()) {
    const float tilt = *this->tilt_;
    invalid_request |= !std::isfinite(tilt) || tilt < 0.0f || tilt > 1.0f;
  }
  if (this->parent_->requires_code_to_open() && (invalid_request || conflicting_request || empty_request)) {
    ESP_LOGW(TAG, "'%s': malformed or conflicting command rejected", this->parent_->get_name().c_str());
    return;
  }
  bool protected_request = this->malformed_request_ || this->tilt_.has_value() || this->toggle_.has_value();
  if (this->position_.has_value()) {
    const float position = *this->position_;
    protected_request |=
        !std::isfinite(position) || position < COVER_CLOSED || position > COVER_OPEN || position > COVER_CLOSED;
  }
  if (this->parent_->requires_code_to_open() && protected_request &&
      !this->parent_->authorize_opening_(this->authorization_code_)) {
    ESP_LOGW(TAG, "'%s': opening authorization required or rejected", this->parent_->get_name().c_str());
    return;
  }
#endif
  this->validate_();
  if (this->stop_) {
    ESP_LOGV(TAG, "  Command: STOP");
  }
  if (this->position_.has_value()) {
    if (traits.get_supports_position()) {
      ESP_LOGV(TAG, "  Position: %.0f%%", *this->position_ * 100.0f);
    } else {
      ESP_LOGV(TAG, "  Command: %s", LOG_STR_ARG(cover_command_to_str(*this->position_)));
    }
  }
  if (this->tilt_.has_value()) {
    ESP_LOGV(TAG, "  Tilt: %.0f%%", *this->tilt_ * 100.0f);
  }
  if (this->toggle_.has_value()) {
    ESP_LOGV(TAG, "  Command: TOGGLE");
  }
  this->parent_->control(*this);
}
const optional<float> &CoverCall::get_position() const { return this->position_; }
const optional<float> &CoverCall::get_tilt() const { return this->tilt_; }
const optional<bool> &CoverCall::get_toggle() const { return this->toggle_; }
void CoverCall::validate_() {
  auto traits = this->parent_->get_traits();
  const char *name = this->parent_->get_name().c_str();

  if (this->position_.has_value()) {
    auto pos = *this->position_;
#ifdef USE_COVER_AUTHORIZATION
    if (!std::isfinite(pos)) {
      ESP_LOGW(TAG, "'%s': non-finite position rejected", name);
      this->position_.reset();
    } else
#endif
        if (!traits.get_supports_position() && pos != COVER_OPEN && pos != COVER_CLOSED) {
      ESP_LOGW(TAG, "'%s': position unsupported", name);
      this->position_.reset();
    } else if (pos < 0.0f || pos > 1.0f) {
      ESP_LOGW(TAG, "'%s': position %.2f out of range", name, pos);
      this->position_ = clamp(pos, 0.0f, 1.0f);
    }
  }
  if (this->tilt_.has_value()) {
    auto tilt = *this->tilt_;
#ifdef USE_COVER_AUTHORIZATION
    if (!std::isfinite(tilt)) {
      ESP_LOGW(TAG, "'%s': non-finite tilt rejected", name);
      this->tilt_.reset();
    } else
#endif
        if (!traits.get_supports_tilt()) {
      ESP_LOGW(TAG, "'%s': tilt unsupported", name);
      this->tilt_.reset();
    } else if (tilt < 0.0f || tilt > 1.0f) {
      ESP_LOGW(TAG, "'%s': tilt %.2f out of range", name, tilt);
      this->tilt_ = clamp(tilt, 0.0f, 1.0f);
    }
  }
  if (this->toggle_.has_value()) {
    if (!traits.get_supports_toggle()) {
      ESP_LOGW(TAG, "'%s': toggle unsupported", name);
      this->toggle_.reset();
    }
  }
  if (this->stop_) {
    if (this->position_.has_value() || this->tilt_.has_value() || this->toggle_.has_value()) {
      ESP_LOGW(TAG, "'%s': cannot position/tilt/toggle when stopping", name);
      this->position_.reset();
      this->tilt_.reset();
      this->toggle_.reset();
    }
  }
}
CoverCall &CoverCall::set_stop(bool stop) {
  this->stop_ = stop;
  return *this;
}
#ifdef USE_COVER_AUTHORIZATION
void Cover::set_authorization_opening(const char *pin, uint8_t max_attempts, uint32_t lockout_ms,
                                      uint32_t preference_key) {
  if (pin == nullptr || max_attempts == 0 || lockout_ms == 0)
    return;
  this->authorization_pin_ = pin;
  this->authorization_pin_length_ = static_cast<uint8_t>(strlen(pin));
  this->authorization_max_attempts_ = max_attempts;
  this->authorization_lockout_ms_ = lockout_ms;
  this->authorization_pref_ = global_preferences->make_preference<AuthorizationState>(preference_key, true);
  this->load_authorization_state_();
}

void Cover::load_authorization_state_() {
  if (this->authorization_state_loaded_)
    return;
  AuthorizationState state{};
  if (this->authorization_pref_.load(&state)) {
    if (state.locked != 0 || state.attempts != 0) {
      this->authorization_attempts_ = this->authorization_max_attempts_;
      this->authorization_locked_ = true;
    }
  }
  this->authorization_lockout_started_ = millis();
  this->authorization_state_loaded_ = true;
}

bool Cover::save_authorization_state_() {
  const AuthorizationState state{this->authorization_attempts_, static_cast<uint8_t>(this->authorization_locked_)};
  return this->authorization_pref_.save(&state) && global_preferences->sync();
}

bool Cover::authorize_opening_(StringRef code) {
  this->load_authorization_state_();
  if (this->authorization_locked_) {
    if (millis() - this->authorization_lockout_started_ < this->authorization_lockout_ms_)
      return false;
    this->authorization_locked_ = false;
    this->authorization_attempts_ = 0;
    if (!this->save_authorization_state_()) {
      this->authorization_locked_ = true;
      this->authorization_lockout_started_ = millis();
      return false;
    }
  }

  if (code.empty())
    return false;

  constexpr size_t MAX_PIN_LENGTH = 12;
  const size_t code_length = code.size();

  uint8_t difference = static_cast<uint8_t>(code_length != this->authorization_pin_length_);
  for (size_t i = 0; i < MAX_PIN_LENGTH; i++) {
    const uint8_t supplied = i < code_length ? static_cast<uint8_t>(code[i]) : 0;
    const uint8_t expected =
        i < this->authorization_pin_length_ ? static_cast<uint8_t>(this->authorization_pin_[i]) : 0;
    difference |= supplied ^ expected;
  }

  if (code_length <= MAX_PIN_LENGTH && difference == 0) {
    if (this->authorization_attempts_ != 0) {
      this->authorization_attempts_ = 0;
      if (!this->save_authorization_state_()) {
        this->authorization_locked_ = true;
        this->authorization_lockout_started_ = millis();
        return false;
      }
    }
    return true;
  }

  const bool first_failed_attempt = this->authorization_attempts_ == 0;
  if (this->authorization_attempts_ < this->authorization_max_attempts_)
    this->authorization_attempts_++;
  if (this->authorization_attempts_ >= this->authorization_max_attempts_) {
    this->authorization_locked_ = true;
    this->authorization_lockout_started_ = millis();
  }
  if ((first_failed_attempt || this->authorization_locked_) && !this->save_authorization_state_()) {
    this->authorization_locked_ = true;
    this->authorization_lockout_started_ = millis();
  }
  return false;
}
#endif
void Cover::publish_state(bool save) {
  this->position = clamp(this->position, 0.0f, 1.0f);
  this->tilt = clamp(this->tilt, 0.0f, 1.0f);

  ESP_LOGV(TAG, "'%s' >>", this->name_.c_str());
  auto traits = this->get_traits();
  if (traits.get_supports_position()) {
    ESP_LOGV(TAG, "  Position: %.0f%%", this->position * 100.0f);
  } else {
    if (this->position == COVER_OPEN) {
      ESP_LOGV(TAG, "  State: OPEN");
    } else if (this->position == COVER_CLOSED) {
      ESP_LOGV(TAG, "  State: CLOSED");
    } else {
      ESP_LOGV(TAG, "  State: UNKNOWN");
    }
  }
  if (traits.get_supports_tilt()) {
    ESP_LOGV(TAG, "  Tilt: %.0f%%", this->tilt * 100.0f);
  }
  ESP_LOGV(TAG, "  Current Operation: %s", LOG_STR_ARG(cover_operation_to_str(this->current_operation)));

  this->state_callback_.call();
#if defined(USE_COVER) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_cover_update(this);
#endif

  if (save) {
    CoverRestoreState restore{};
    memset(&restore, 0, sizeof(restore));
    restore.position = this->position;
    if (traits.get_supports_tilt()) {
      restore.tilt = this->tilt;
    }
    this->rtc_.save(&restore);
  }
}
optional<CoverRestoreState> Cover::restore_state_() {
  this->rtc_ = this->make_entity_preference<CoverRestoreState>();
  CoverRestoreState recovered{};
  if (!this->rtc_.load(&recovered))
    return {};
  return recovered;
}

CoverCall CoverRestoreState::to_call(Cover *cover) {
  auto call = cover->make_call();
  auto traits = cover->get_traits();
  call.set_position(this->position);
  if (traits.get_supports_tilt())
    call.set_tilt(this->tilt);
  return call;
}
void CoverRestoreState::apply(Cover *cover) {
  cover->position = this->position;
  cover->tilt = this->tilt;
  cover->publish_state();
}

}  // namespace esphome::cover
