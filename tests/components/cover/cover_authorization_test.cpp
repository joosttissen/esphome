#include <gtest/gtest.h>
#include <cmath>

#include "esphome/components/cover/cover.h"

namespace esphome::cover::testing {
namespace {
class TestCover : public Cover {
 public:
  CoverTraits get_traits() override {
    CoverTraits traits;
    traits.set_supports_position(true);
    traits.set_supports_tilt(true);
    traits.set_supports_toggle(true);
    return traits;
  }

  void configure_auth(const char *pin, uint8_t max_attempts = 5, uint32_t lockout_ms = 300000,
                      uint32_t preference_key = 0) {
    static uint32_t next_hash = 0x43A119D2;
    this->set_authorization_opening(pin, max_attempts, lockout_ms, preference_key == 0 ? next_hash++ : preference_key);
    this->configure_entity_("cover", next_hash++, 0);
  }

  void expire_lockout() { this->authorization_lockout_started_ = millis() - this->authorization_lockout_ms_; }

  int calls{0};

 protected:
  void control(const CoverCall &) override { this->calls++; }
};
}  // namespace

TEST(CoverAuthorization, RequiresCodeOnlyWhenConfigured) {
  TestCover cover;
  EXPECT_FALSE(cover.requires_code_to_open());
  cover.configure_auth("1234");
  EXPECT_TRUE(cover.requires_code_to_open());
}

TEST(CoverAuthorization, EmptyAndMissingCodesDoNotConsumeAttempts) {
  TestCover cover;
  cover.configure_auth("1234", 2);

  cover.make_call().set_command_open().perform();
  cover.make_call().set_command_open().set_authorization_code(StringRef("")).perform();
  cover.make_call().set_command_open().set_authorization_code(StringRef("0000")).perform();
  EXPECT_EQ(cover.calls, 0);

  cover.make_call().set_command_open().set_authorization_code(StringRef("1234")).perform();
  EXPECT_EQ(cover.calls, 1);
}

TEST(CoverAuthorization, ProtectsToggleTiltMalformedAndNonzeroPosition) {
  TestCover cover;
  cover.configure_auth("1234");

  cover.make_call().set_command_toggle().perform();
  cover.make_call().set_position(0.0f).set_tilt(0.0f).perform();
  cover.make_call().set_position(NAN).perform();
  cover.make_call().set_position(0.5f).perform();
  cover.make_call().perform();
  cover.make_call().set_position(1.1f).set_authorization_code(StringRef("1234")).perform();
  cover.make_call().set_tilt(-0.1f).set_authorization_code(StringRef("1234")).perform();
  cover.make_call().set_command("unknown").set_authorization_code(StringRef("1234")).perform();
  cover.make_call().set_command_stop().set_position(0.0f).set_authorization_code(StringRef("1234")).perform();
  cover.make_call().set_command_toggle().set_position(0.5f).set_authorization_code(StringRef("1234")).perform();
  EXPECT_EQ(cover.calls, 0);
}

TEST(CoverAuthorization, CloseAndPureStopRemainAvailableDuringLockout) {
  TestCover cover;
  cover.configure_auth("1234", 1, 10000);

  cover.make_call().set_command_open().set_authorization_code(StringRef("0000")).perform();
  cover.make_call().set_command_close().perform();
  cover.make_call().set_command_stop().perform();
  EXPECT_EQ(cover.calls, 2);
}

TEST(CoverAuthorization, LockoutExpiresUsingWrapSafeElapsedTime) {
  TestCover cover;
  cover.configure_auth("1234", 1, 20);

  cover.make_call().set_command_open().set_authorization_code(StringRef("0000")).perform();
  cover.make_call().set_command_open().set_authorization_code(StringRef("1234")).perform();
  EXPECT_EQ(cover.calls, 0);

  cover.expire_lockout();
  cover.make_call().set_command_open().set_authorization_code(StringRef("1234")).perform();
  EXPECT_EQ(cover.calls, 1);
}

TEST(CoverAuthorization, FailedAttemptsPersistAcrossCoverReconstruction) {
  constexpr uint32_t PREFERENCE_KEY = 0xA4217592;
  TestCover first;
  first.configure_auth("1234", 5, 300000, PREFERENCE_KEY);
  first.make_call().set_command_open().set_authorization_code(StringRef("0000")).perform();

  TestCover restarted;
  restarted.configure_auth("1234", 5, 300000, PREFERENCE_KEY);
  restarted.make_call().set_command_open().set_authorization_code(StringRef("1234")).perform();
  EXPECT_EQ(restarted.calls, 0);

  restarted.expire_lockout();
  restarted.make_call().set_command_open().set_authorization_code(StringRef("1234")).perform();
  EXPECT_EQ(restarted.calls, 1);
}

}  // namespace esphome::cover::testing
