// Basic tests for prolink_cpp data helpers.
#include "prolink/prolink.h"

#include <gtest/gtest.h>

TEST(BeatInfoTest, EffectiveBpmUsesPitchMultiplier) {
  prolink::BeatInfo info;
  info.bpm = 12000;
  info.pitch = prolink::kNeutralPitch;
  EXPECT_NEAR(info.effective_bpm(), 120.0, 0.001);

  info.pitch = static_cast<uint32_t>(prolink::kNeutralPitch * 1.5);
  EXPECT_NEAR(info.effective_bpm(), 180.0, 0.001);
}

TEST(StatusInfoTest, EffectiveBpmHandlesMissingTrack) {
  prolink::StatusInfo info;
  info.bpm.reset();
  info.pitch = prolink::kNeutralPitch;
  EXPECT_FALSE(info.effective_bpm().has_value());

  info.bpm = 12850;
  EXPECT_NEAR(info.effective_bpm().value(), 128.5, 0.001);
}

TEST(ConstantsTest, PacketTypesMatchSpec) {
  EXPECT_EQ(static_cast<uint8_t>(prolink::PacketType::kDeviceKeepAlive), 0x06);
  EXPECT_EQ(static_cast<uint8_t>(prolink::PacketType::kCdjStatus), 0x0a);
  EXPECT_EQ(static_cast<uint8_t>(prolink::PacketType::kMasterHandoffRequest), 0x26);
  EXPECT_EQ(static_cast<uint8_t>(prolink::PacketType::kMasterHandoffResponse), 0x27);
  EXPECT_EQ(static_cast<uint8_t>(prolink::PacketType::kBeat), 0x28);
  EXPECT_EQ(static_cast<uint8_t>(prolink::PacketType::kSyncControl), 0x2a);
}

TEST(ConstantsTest, SyncCommandsMatchSpec) {
  EXPECT_EQ(static_cast<uint8_t>(prolink::SyncCommand::kEnableSync), 0x10);
  EXPECT_EQ(static_cast<uint8_t>(prolink::SyncCommand::kDisableSync), 0x20);
  EXPECT_EQ(static_cast<uint8_t>(prolink::SyncCommand::kBecomeMaster), 0x01);
}

TEST(ConstantsTest, NeutralPitchIsOneX) {
  prolink::BeatInfo info;
  info.bpm = 10000;
  info.pitch = prolink::kNeutralPitch;
  EXPECT_NEAR(info.effective_bpm(), 100.0, 0.001);
}

TEST(ConfigTest, DefaultsMatchExpected) {
  prolink::Config config;
  EXPECT_EQ(config.device_number, 0x07);
  EXPECT_EQ(config.device_type, 0x01);
  EXPECT_EQ(config.status_interval_ms, 200);
  EXPECT_EQ(config.announce_interval_ms, 1500);
  EXPECT_EQ(config.beats_per_bar, 4);
  EXPECT_EQ(config.device_timeout.count(), 4000);
  EXPECT_EQ(config.device_prune_interval.count(), 1000);
  EXPECT_EQ(config.master_request_retry_interval.count(), 1000);
  EXPECT_EQ(config.master_request_timeout.count(), 5000);
  EXPECT_EQ(config.master_request_max_retries, 3);
  EXPECT_FALSE(config.follow_master);
}

TEST(ConfigTest, ValidateRejectsEmptyDeviceName) {
  prolink::Config config;
  config.device_name.clear();
  std::string error;
  EXPECT_FALSE(config.Validate(&error));
  EXPECT_FALSE(error.empty());
}
