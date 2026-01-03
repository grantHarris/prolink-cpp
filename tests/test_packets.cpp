// Packet layout tests for sync and master handoff packets.
#include "prolink/test_hooks.h"

#include <gtest/gtest.h>

namespace {

constexpr uint8_t kHeader[] = {
    0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c,
};

void ExpectHeader(const std::vector<uint8_t>& packet) {
  ASSERT_GE(packet.size(), sizeof(kHeader));
  for (size_t i = 0; i < sizeof(kHeader); ++i) {
    EXPECT_EQ(packet[i], kHeader[i]);
  }
}

void ExpectDeviceName(const std::vector<uint8_t>& packet,
                      const std::string& name) {
  ASSERT_GE(packet.size(), 0x1f);
  for (size_t i = 0; i < name.size(); ++i) {
    EXPECT_EQ(packet[0x0b + i], static_cast<uint8_t>(name[i]));
  }
  for (size_t i = name.size(); i < 20; ++i) {
    EXPECT_EQ(packet[0x0b + i], 0x00);
  }
}

}  // namespace

TEST(PacketLayoutTest, SyncControlPacketOffsets) {
  const uint8_t device = 0x03;
  const std::string name = "test-device";
  const auto packet = prolink::test::BuildSyncControlPacket(
      device, name, prolink::SyncCommand::kEnableSync);

  EXPECT_EQ(packet.size(), 0x2c);
  ExpectHeader(packet);
  ExpectDeviceName(packet, name);
  EXPECT_EQ(packet[0x0a], 0x2a);
  EXPECT_EQ(packet[0x1f], 0x01);
  EXPECT_EQ(packet[0x20], 0x00);
  EXPECT_EQ(packet[0x21], device);
  EXPECT_EQ(packet[0x22], 0x00);
  EXPECT_EQ(packet[0x23], 0x08);
  EXPECT_EQ(packet[0x27], device);
  EXPECT_EQ(packet[0x2b], static_cast<uint8_t>(prolink::SyncCommand::kEnableSync));
}

TEST(PacketLayoutTest, MasterHandoffRequestOffsets) {
  const uint8_t device = 0x04;
  const std::string name = "handoff";
  const auto packet = prolink::test::BuildMasterHandoffRequestPacket(device, name);

  EXPECT_EQ(packet.size(), 0x28);
  ExpectHeader(packet);
  ExpectDeviceName(packet, name);
  EXPECT_EQ(packet[0x0a], 0x26);
  EXPECT_EQ(packet[0x1f], 0x01);
  EXPECT_EQ(packet[0x20], 0x00);
  EXPECT_EQ(packet[0x21], device);
  EXPECT_EQ(packet[0x22], 0x00);
  EXPECT_EQ(packet[0x23], 0x04);
  EXPECT_EQ(packet[0x27], device);
}

TEST(PacketLayoutTest, MasterHandoffResponseOffsets) {
  const uint8_t device = 0x02;
  const std::string name = "responder";
  const auto packet =
      prolink::test::BuildMasterHandoffResponsePacket(device, name, true);

  EXPECT_EQ(packet.size(), 0x2c);
  ExpectHeader(packet);
  ExpectDeviceName(packet, name);
  EXPECT_EQ(packet[0x0a], 0x27);
  EXPECT_EQ(packet[0x1f], 0x01);
  EXPECT_EQ(packet[0x20], 0x00);
  EXPECT_EQ(packet[0x21], device);
  EXPECT_EQ(packet[0x22], 0x00);
  EXPECT_EQ(packet[0x23], 0x08);
  EXPECT_EQ(packet[0x27], device);
  EXPECT_EQ(packet[0x2b], 0x01);
}
