// Tests for configuration validation.
#include "prolink/prolink.h"

#include <gtest/gtest.h>

TEST(ConfigValidationTest, RejectsZeroDeviceNumber) {
  prolink::Config config;
  config.device_number = 0;
  std::string error;
  EXPECT_FALSE(config.Validate(&error));
  EXPECT_NE(error.find("device_number"), std::string::npos);
}

TEST(ConfigValidationTest, RejectsInvalidDeviceIp) {
  prolink::Config config;
  config.device_ip = "999.999.999.999";
  std::string error;
  EXPECT_FALSE(config.Validate(&error));
  EXPECT_NE(error.find("device_ip"), std::string::npos);
}

TEST(ConfigValidationTest, RejectsInvalidBroadcastAddress) {
  prolink::Config config;
  config.broadcast_address = "not-an-ip";
  std::string error;
  EXPECT_FALSE(config.Validate(&error));
  EXPECT_NE(error.find("broadcast_address"), std::string::npos);
}

TEST(ConfigValidationTest, RejectsNonPositiveIntervals) {
  prolink::Config config;
  config.status_interval_ms = 0;
  std::string error;
  EXPECT_FALSE(config.Validate(&error));
  EXPECT_NE(error.find("intervals"), std::string::npos);
}

TEST(ConfigValidationTest, RejectsNonPositiveTimeouts) {
  prolink::Config config;
  config.device_timeout = std::chrono::milliseconds(0);
  std::string error;
  EXPECT_FALSE(config.Validate(&error));
  EXPECT_NE(error.find("device timeouts"), std::string::npos);
}

TEST(ConfigValidationTest, RejectsCaptureAndReplayTogether) {
  prolink::Config config;
  config.capture_file = "capture.bin";
  config.replay_file = "replay.bin";
  std::string error;
  EXPECT_FALSE(config.Validate(&error));
  EXPECT_NE(error.find("mutually exclusive"), std::string::npos);
}

TEST(ConfigValidationTest, RejectsTimeoutShorterThanRetryInterval) {
  prolink::Config config;
  config.master_request_retry_interval = std::chrono::milliseconds(5000);
  config.master_request_timeout = std::chrono::milliseconds(1000);
  std::string error;
  EXPECT_FALSE(config.Validate(&error));
  EXPECT_NE(error.find("master_request_timeout"), std::string::npos);
}

TEST(ConfigValidationTest, AcceptsDefaults) {
  prolink::Config config;
  std::string error;
  EXPECT_TRUE(config.Validate(&error));
}
