#pragma once

#include "prolink/prolink.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace prolink {

#ifdef PROLINK_TESTING
namespace test {

struct BeatClockSnapshot {
  uint32_t beat = 1;
  uint8_t beat_within_bar = 1;
  double tempo_bpm = 120.0;
  double beat_interval_ms = 500.0;
  double bar_interval_ms = 2000.0;
};

class BeatClockTester {
 public:
  explicit BeatClockTester(int beats_per_bar);
  ~BeatClockTester();

  BeatClockTester(const BeatClockTester&) = delete;
  BeatClockTester& operator=(const BeatClockTester&) = delete;

  void SetTempo(double bpm);
  void SetPlaying(bool playing);
  void AlignToBeatNumber(uint32_t beat, uint8_t beat_within_bar,
                         std::chrono::steady_clock::time_point when);
  void AlignToBeatWithinBar(uint8_t beat_within_bar,
                            std::chrono::steady_clock::time_point when);
  BeatClockSnapshot Snapshot(std::chrono::steady_clock::time_point now) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::vector<uint8_t> BuildSyncControlPacket(uint8_t device_number,
                                            const std::string& device_name,
                                            SyncCommand command);

std::vector<uint8_t> BuildMasterHandoffRequestPacket(uint8_t device_number,
                                                     const std::string& device_name);

std::vector<uint8_t> BuildMasterHandoffResponsePacket(uint8_t device_number,
                                                      const std::string& device_name,
                                                      bool accepted);

std::vector<uint8_t> BuildBeatPacket(uint8_t device_number,
                                     const std::string& device_name,
                                     uint32_t bpm,
                                     uint32_t pitch,
                                     uint8_t beat_within_bar,
                                     uint32_t next_beat_ms,
                                     uint32_t next_bar_ms);

std::vector<uint8_t> BuildStatusPacket(uint8_t device_number,
                                       const std::string& device_name,
                                       uint32_t bpm,
                                       uint32_t pitch,
                                       uint32_t beat_number,
                                       uint8_t beat_within_bar,
                                       bool is_master,
                                       bool is_synced,
                                       bool is_playing,
                                       uint8_t master_handoff_to);

std::vector<uint8_t> BuildKeepAlivePacket(uint8_t device_number,
                                          uint8_t device_type,
                                          const std::string& device_name,
                                          const std::array<uint8_t, 6>& mac_address,
                                          const std::string& ip_address);

bool ParseBeatPacket(const std::vector<uint8_t>& data, BeatInfo* out);
bool ParseStatusPacket(const std::vector<uint8_t>& data, StatusInfo* out);
bool ParseKeepAlivePacket(const std::vector<uint8_t>& data, DeviceInfo* out);

void InjectKeepAlive(Session& session,
                     uint8_t device_number,
                     uint8_t device_type,
                     const std::string& device_name,
                     const std::string& ip_address,
                     const std::array<uint8_t, 6>& mac_address);

void SetDeviceLastSeen(Session& session,
                       uint8_t device_number,
                       std::chrono::steady_clock::time_point when);

void PruneDevices(Session& session,
                  std::chrono::steady_clock::time_point now);

size_t GetDeviceRecordCount(Session& session);

}  // namespace test
#endif

}  // namespace prolink
