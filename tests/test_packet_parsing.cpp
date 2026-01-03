// Tests for parsing beat, status, and keep-alive packets.
#include "prolink/test_hooks.h"

#include <gtest/gtest.h>

TEST(PacketParsingTest, ParseBeatPacket) {
  const uint8_t device = 0x01;
  const std::string name = "CDJ-1";
  const auto packet = prolink::test::BuildBeatPacket(
      device, name, 12800, prolink::kNeutralPitch, 3, 500, 1500);

  prolink::BeatInfo info;
  ASSERT_TRUE(prolink::test::ParseBeatPacket(packet, &info));
  EXPECT_EQ(info.device_number, device);
  EXPECT_EQ(info.device_name, name);
  EXPECT_EQ(info.bpm, 12800u);
  EXPECT_EQ(info.pitch, prolink::kNeutralPitch);
  EXPECT_EQ(info.beat_within_bar, 3);
  EXPECT_EQ(info.next_beat_ms, 500u);
  EXPECT_EQ(info.next_bar_ms, 1500u);
}

TEST(PacketParsingTest, RejectUndersizedBeatPacket) {
  std::vector<uint8_t> packet(10, 0x00);
  prolink::BeatInfo info;
  EXPECT_FALSE(prolink::test::ParseBeatPacket(packet, &info));
}

TEST(PacketParsingTest, SanitizeInvalidBeatWithinBar) {
  const auto packet = prolink::test::BuildBeatPacket(
      0x02, "CDJ-2", 12000, prolink::kNeutralPitch, 0, 500, 1500);

  prolink::BeatInfo info;
  ASSERT_TRUE(prolink::test::ParseBeatPacket(packet, &info));
  EXPECT_EQ(info.beat_within_bar, 1);
}

TEST(PacketParsingTest, ParseStatusPacket) {
  const uint8_t device = 0x03;
  const std::string name = "CDJ-3";
  const auto packet = prolink::test::BuildStatusPacket(
      device, name, 12400, prolink::kNeutralPitch, 128, 2,
      true, true, true, 0x04);

  prolink::StatusInfo info;
  ASSERT_TRUE(prolink::test::ParseStatusPacket(packet, &info));
  EXPECT_EQ(info.device_number, device);
  EXPECT_EQ(info.device_name, name);
  ASSERT_TRUE(info.bpm.has_value());
  EXPECT_EQ(info.bpm.value(), 12400u);
  ASSERT_TRUE(info.beat.has_value());
  EXPECT_EQ(info.beat.value(), 128u);
  EXPECT_TRUE(info.is_master);
  EXPECT_TRUE(info.is_synced);
  EXPECT_TRUE(info.is_playing);
  EXPECT_EQ(info.master_handoff_to, 0x04);
  EXPECT_EQ(info.beat_within_bar, 2);
}

TEST(PacketParsingTest, StatusPacketMissingTrackClearsFields) {
  const auto packet = prolink::test::BuildStatusPacket(
      0x04, "CDJ-4", 0xffff, prolink::kNeutralPitch, 0xffffffff, 1,
      false, false, false, 0xff);

  prolink::StatusInfo info;
  ASSERT_TRUE(prolink::test::ParseStatusPacket(packet, &info));
  EXPECT_FALSE(info.bpm.has_value());
  EXPECT_FALSE(info.beat.has_value());
}

TEST(PacketParsingTest, ParseKeepAlivePacket) {
  const std::array<uint8_t, 6> mac = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  const auto packet = prolink::test::BuildKeepAlivePacket(
      0x01, 0x01, "CDJ-1", mac, "192.168.0.10");

  prolink::DeviceInfo info;
  ASSERT_TRUE(prolink::test::ParseKeepAlivePacket(packet, &info));
  EXPECT_EQ(info.device_number, 0x01);
  EXPECT_EQ(info.device_type, 0x01);
  EXPECT_EQ(info.device_name, "CDJ-1");
  EXPECT_EQ(info.ip_address, "192.168.0.10");
  EXPECT_EQ(info.mac_address, mac);
}

TEST(PacketParsingTest, RejectInvalidHeader) {
  auto packet = prolink::test::BuildBeatPacket(
      0x01, "CDJ-1", 12000, prolink::kNeutralPitch, 1, 500, 1500);
  packet[0] = 0x00;

  prolink::BeatInfo info;
  EXPECT_FALSE(prolink::test::ParseBeatPacket(packet, &info));
}
