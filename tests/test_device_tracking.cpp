// Tests for device discovery tracking and expiry.
#include "prolink/test_hooks.h"

#include <gtest/gtest.h>

TEST(DeviceTrackingTest, SeenAndUpdatedEvents) {
  prolink::Config config;
  prolink::Session session(config);

  std::vector<prolink::DeviceEvent> events;
  session.SetDeviceEventCallback([&](const prolink::DeviceEvent& event) {
    events.push_back(event);
  });

  const std::array<uint8_t, 6> mac = {0, 1, 2, 3, 4, 5};
  prolink::test::InjectKeepAlive(session, 1, 0x01, "CDJ-1", "192.168.0.2", mac);

  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, prolink::DeviceEventType::kSeen);
  EXPECT_EQ(events[0].device.device_number, 1);
  EXPECT_EQ(events[0].device.device_name, "CDJ-1");

  prolink::test::InjectKeepAlive(session, 1, 0x01, "CDJ-1B", "192.168.0.2", mac);

  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[1].type, prolink::DeviceEventType::kUpdated);
  EXPECT_EQ(events[1].device.device_name, "CDJ-1B");
}

TEST(DeviceTrackingTest, ExpiredDevicesPruned) {
  prolink::Config config;
  config.device_timeout = std::chrono::milliseconds(100);
  prolink::Session session(config);

  std::vector<prolink::DeviceEvent> events;
  session.SetDeviceEventCallback([&](const prolink::DeviceEvent& event) {
    events.push_back(event);
  });

  const std::array<uint8_t, 6> mac = {9, 8, 7, 6, 5, 4};
  prolink::test::InjectKeepAlive(session, 2, 0x01, "CDJ-2", "192.168.0.3", mac);
  ASSERT_EQ(session.GetDevices().size(), 1u);

  const auto now = std::chrono::steady_clock::now();
  prolink::test::SetDeviceLastSeen(session, 2,
                                   now - config.device_timeout - std::chrono::milliseconds(1));
  prolink::test::PruneDevices(session, now);

  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().type, prolink::DeviceEventType::kExpired);
  EXPECT_TRUE(session.GetDevices().empty());

  prolink::test::SetDeviceLastSeen(session, 2,
                                   now - (config.device_timeout * 11));
  prolink::test::PruneDevices(session, now);
  EXPECT_EQ(prolink::test::GetDeviceRecordCount(session), 0u);
}
