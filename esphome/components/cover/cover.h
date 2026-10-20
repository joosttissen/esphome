#pragma once

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/string_ref.h"

#include "cover_traits.h"

namespace esphome::cover {

static constexpr float COVER_OPEN = 1.0f;
static constexpr float COVER_CLOSED = 0.0f;

#define LOG_COVER(prefix, type, obj) \
  if ((obj) != nullptr) { \
    ESP_LOGCONFIG(TAG, "%s%s '%s'", prefix, LOG_STR_LITERAL(type), (obj)->get_name().c_str()); \
    auto traits_ = (obj)->get_traits(); \
    if (traits_.get_is_assumed_state()) { \
      ESP_LOGCONFIG(TAG, "%s  Assumed State: YES", prefix); \
    } \
    LOG_ENTITY_DEVICE_CLASS(TAG, prefix, *(obj)); \
  }

class Cover;

class CoverCall {
 public:
  CoverCall(Cover *parent);

  /// Set the command as a string, "STOP", "OPEN", "CLOSE", "TOGGLE".
  CoverCall &set_command(const char *command);
  /// Set the command to open the cover.
  CoverCall &set_command_open();
  /// Set the command to close the cover.
  CoverCall &set_command_close();
  /// Set the command to stop the cover.
  CoverCall &set_command_stop();
  /// Set the command to toggle the cover.
  CoverCall &set_command_toggle();
  /// Set the call to a certain target position.
  CoverCall &set_position(float position);
  /// Set the call to a certain target tilt.
  CoverCall &set_tilt(float tilt);
  /// Set the authorization code for protected cover operations.
  CoverCall &set_authorization_code(StringRef code);
  /// Set whether this cover call should stop the cover.
  CoverCall &set_stop(bool stop);

  /// Perform the cover call.
  void perform();

  const optional<float> &get_position() const;
  bool get_stop() const { return this->stop_; }
  const optional<float> &get_tilt() const;
  const optional<bool> &get_toggle() const;

 protected:
  void validate_();

  Cover *parent_;
  bool stop_{false};
  optional<float> position_{};
  optional<float> tilt_{};
  optional<bool> toggle_{};
#ifdef USE_COVER_AUTHORIZATION
  bool malformed_request_{false};
  StringRef authorization_code_{};
#endif
};

/// Struct used to store the restored state of a cover
struct CoverRestoreState {
  float position;
  float tilt;

  /// Convert this struct to a cover call that can be performed.
  CoverCall to_call(Cover *cover);
  /// Apply these settings to the cover
  void apply(Cover *cover);
} __attribute__((packed));

/// Enum encoding the current operation of a cover.
enum CoverOperation : uint8_t {
  /// The cover is currently idle (not moving)
  COVER_OPERATION_IDLE = 0,
  /// The cover is currently opening.
  COVER_OPERATION_OPENING,
  /// The cover is currently closing.
  COVER_OPERATION_CLOSING,
};

const LogString *cover_operation_to_str(CoverOperation op);

/** Base class for all cover devices.
 *
 * Covers currently have three properties:
 *  - position - The current position of the cover from 0.0 (fully closed) to 1.0 (fully open).
 *    For covers with only binary OPEN/CLOSED position this will always be either 0.0 or 1.0
 *  - tilt - The tilt value of the cover from 0.0 (closed) to 1.0 (closed)
 *  - current_operation - The operation the cover is currently performing, this can
 *    be one of IDLE, OPENING and CLOSING.
 *
 * For users: All cover operations must be performed over the .make_call() interface.
 * To command a cover, use .make_call() to create a call object, set all properties
 * you wish to set, and activate the command with .perform().
 * For reading out the current values of the cover, use the public .position, .tilt etc
 * properties (these are read-only for users)
 *
 * For integrations: Integrations must implement two methods: control() and get_traits().
 * Control will be called with the arguments supplied by the user and should be used
 * to control all values of the cover. Also implement get_traits() to return what operations
 * the cover supports.
 */
class Cover : public EntityBase {
 public:
  explicit Cover();

  /// The current operation of the cover (idle, opening, closing).
  CoverOperation current_operation{COVER_OPERATION_IDLE};
  /** The position of the cover from 0.0 (fully closed) to 1.0 (fully open).
   *
   * For binary covers this is always equals to 0.0 or 1.0 (see also COVER_OPEN and
   * COVER_CLOSED constants).
   */
  float position;
  /// The current tilt value of the cover from 0.0 to 1.0.
  float tilt{COVER_OPEN};

  /// Construct a new cover call used to control the cover.
  CoverCall make_call() { return {this}; }

  /// Whether opening operations on this cover require an authorization code.
  virtual bool requires_code_to_open() const {
#ifdef USE_COVER_AUTHORIZATION
    return this->authorization_pin_ != nullptr;
#else
    return false;
#endif
  }

#ifdef USE_COVER_AUTHORIZATION
  /// Configure the PIN and brute-force limits for opening operations.
  virtual void set_authorization_opening(const char *pin, uint8_t max_attempts, uint32_t lockout_ms,
                                         uint32_t preference_key);
#endif

  template<typename F> void add_on_state_callback(F &&f) { this->state_callback_.add(std::forward<F>(f)); }

  /** Publish the current state of the cover.
   *
   * First set the .position, .tilt, etc values and then call this method
   * to publish the state of the cover.
   *
   * @param save Whether to save the updated values in RTC area.
   */
  void publish_state(bool save = true);

  virtual CoverTraits get_traits() = 0;

  /// Helper method to check if the cover is fully open. Equivalent to comparing .position against 1.0
  bool is_fully_open() const { return this->position == COVER_OPEN; }
  /// Helper method to check if the cover is fully closed. Equivalent to comparing .position against 0.0
  bool is_fully_closed() const { return this->position == COVER_CLOSED; }

 protected:
  friend CoverCall;

#ifdef USE_COVER_AUTHORIZATION
  struct AuthorizationState {
    uint8_t attempts;
    uint8_t locked;
  } __attribute__((packed));

  bool authorize_opening_(StringRef code);
  void load_authorization_state_();
  bool save_authorization_state_();

  const char *authorization_pin_{nullptr};
  uint8_t authorization_pin_length_{0};
  uint8_t authorization_max_attempts_{5};
  uint8_t authorization_attempts_{0};
  uint32_t authorization_lockout_ms_{300000};
  uint32_t authorization_lockout_started_{0};
  bool authorization_locked_{false};
  bool authorization_state_loaded_{false};
  ESPPreferenceObject authorization_pref_;
#endif

  virtual void control(const CoverCall &call) = 0;

  optional<CoverRestoreState> restore_state_();

  LazyCallbackManager<void()> state_callback_{};

  ESPPreferenceObject rtc_;
};

}  // namespace esphome::cover
