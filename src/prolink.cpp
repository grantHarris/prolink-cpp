#include "prolink/prolink.h"
#include "prolink/test_hooks.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace prolink {
namespace {

constexpr uint8_t kProlinkHeader[10] = {
    0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c,
};

constexpr size_t kHeaderSize = sizeof(kProlinkHeader);
constexpr size_t kPacketTypeOffset = 0x0a;
constexpr size_t kDeviceNameOffset = 0x0b;
constexpr size_t kPayloadOffset = 0x1f;

constexpr size_t kOffsetDeviceNumber = 0x21;

constexpr size_t kOffsetBeatNext = 0x24;
constexpr size_t kOffsetBeatNextBar = 0x2c;
constexpr size_t kOffsetBeatPitch = 0x55;
constexpr size_t kOffsetBeatBpm = 0x5a;
constexpr size_t kOffsetBeatWithinBar = 0x5c;

constexpr size_t kOffsetStatusPitch = 0x8d;
constexpr size_t kOffsetStatusBpm = 0x92;
constexpr size_t kOffsetStatusFlags = 0x89;
constexpr size_t kOffsetStatusBeat = 0xa0;
constexpr size_t kOffsetStatusBeatWithinBar = 0xa6;
constexpr size_t kOffsetStatusMasterHandoff = 0x9f;

constexpr size_t kOffsetKeepAliveDeviceNumber = 0x24;
constexpr size_t kOffsetKeepAliveDeviceType = 0x25;
constexpr size_t kOffsetKeepAliveMac = 0x26;
constexpr size_t kOffsetKeepAliveIp = 0x2c;

// Payload offsets (relative to payload start at 0x1f).
constexpr size_t kControlPayloadSize = 0x0d;
constexpr size_t kControlPayloadDeviceNumber = 0x02;
constexpr size_t kControlPayloadSender = 0x08;
constexpr size_t kControlPayloadCommand = 0x0c;

constexpr size_t kHandoffRequestPayloadSize = 0x09;

constexpr size_t kOffsetMasterHandoffAccepted = kPayloadOffset + kControlPayloadCommand;

constexpr size_t kOffsetBeatPayloadDeviceNumber = 0x02;
constexpr size_t kOffsetBeatPayloadInterval = 0x05;
constexpr size_t kOffsetBeatPayloadInterval2 = 0x09;
constexpr size_t kOffsetBeatPayloadNextBar = 0x0d;
constexpr size_t kOffsetBeatPayloadInterval4 = 0x11;
constexpr size_t kOffsetBeatPayloadNextBar2 = 0x15;
constexpr size_t kOffsetBeatPayloadInterval8 = 0x19;
constexpr size_t kOffsetBeatPayloadPitch = 0x36;
constexpr size_t kOffsetBeatPayloadBpm = 0x3b;
constexpr size_t kOffsetBeatPayloadBeatWithinBar = 0x3d;
constexpr size_t kOffsetBeatPayloadDeviceNumber2 = 0x40;

constexpr size_t kOffsetStatusPayloadDeviceNumber = 0x02;
constexpr size_t kOffsetStatusPayloadDeviceNumber2 = 0x05;
constexpr size_t kOffsetStatusPayloadPlayingFlag = 0x08;
constexpr size_t kOffsetStatusPayloadDeviceNumber3 = 0x09;
constexpr size_t kOffsetStatusPayloadPlayState = 0x5c;
constexpr size_t kOffsetStatusPayloadFlagByte = 0x6a;
constexpr size_t kOffsetStatusPayloadPlayState2 = 0x6c;
constexpr size_t kOffsetStatusPayloadPitch = 0x6e;
constexpr size_t kOffsetStatusPayloadBpm = 0x73;
constexpr size_t kOffsetStatusPayloadPlayState3 = 0x7e;
constexpr size_t kOffsetStatusPayloadMasterFlag = 0x7f;
constexpr size_t kOffsetStatusPayloadMasterHandoff = 0x80;
constexpr size_t kOffsetStatusPayloadBeatNumber = 0x81;
constexpr size_t kOffsetStatusPayloadBeatWithinBar = 0x87;
constexpr size_t kOffsetStatusPayloadPacketCounter = 0xa9;

constexpr size_t kMaxReplayPacketSize = 2048;

constexpr uint8_t kStatusFlagMaster = 0x20;
constexpr uint8_t kStatusFlagSynced = 0x10;
constexpr uint8_t kStatusFlagPlaying = 0x40;

constexpr size_t kBeatPacketSize = 96;
constexpr size_t kStatusMinimumSize = 0xc8;

constexpr uint16_t kMaxUint16 = 0xffff;
constexpr uint32_t kMaxUint32 = 0xffffffff;
constexpr size_t kKeepAlivePacketSize = 0x36;

// Forward declarations
void LogError(const std::string& message, const Config* config);

// Read big-endian integers from packet bytes.
uint16_t ReadBe16(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

uint32_t ReadBe24(const uint8_t* data, size_t offset) {
  return (static_cast<uint32_t>(data[offset]) << 16) |
         (static_cast<uint32_t>(data[offset + 1]) << 8) |
         static_cast<uint32_t>(data[offset + 2]);
}

uint32_t ReadBe32(const uint8_t* data, size_t offset) {
  return (static_cast<uint32_t>(data[offset]) << 24) |
         (static_cast<uint32_t>(data[offset + 1]) << 16) |
         (static_cast<uint32_t>(data[offset + 2]) << 8) |
         static_cast<uint32_t>(data[offset + 3]);
}

// Write big-endian integers into packet payloads.
void WriteBe16(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
  data[offset] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[offset + 1] = static_cast<uint8_t>(value & 0xff);
}

void WriteBe24(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
  data[offset] = static_cast<uint8_t>((value >> 16) & 0xff);
  data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[offset + 2] = static_cast<uint8_t>(value & 0xff);
}

void WriteBe32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
  data[offset] = static_cast<uint8_t>((value >> 24) & 0xff);
  data[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xff);
  data[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[offset + 3] = static_cast<uint8_t>(value & 0xff);
}

uint32_t SafeMul(uint32_t a, uint32_t b) {
  const uint64_t result = static_cast<uint64_t>(a) * b;
  return result > kMaxUint32 ? kMaxUint32 : static_cast<uint32_t>(result);
}

// Validate the 10-byte magic header used by Pro DJ Link UDP packets.
bool HasHeader(const uint8_t* data, size_t length) {
  if (length < kHeaderSize) {
    return false;
  }
  return std::memcmp(data, kProlinkHeader, kHeaderSize) == 0;
}

// Extract and trim the 20-byte device name field at a specific offset.
std::string ParseDeviceNameAt(const uint8_t* data, size_t length, size_t offset) {
  if (length < offset + kDeviceNameLength) {
    return {};
  }
  std::string name(reinterpret_cast<const char*>(data + offset),
                   kDeviceNameLength);
  auto null_pos = name.find('\0');
  if (null_pos != std::string::npos) {
    name.resize(null_pos);
  }
  // Device names are space-padded in packets; preserve intentional leading spaces.
  while (!name.empty() && name.back() == ' ') {
    name.pop_back();
  }
  return name;
}

// Extract and trim the 20-byte device name field.
std::string ParseDeviceName(const uint8_t* data, size_t length) {
  return ParseDeviceNameAt(data, length, kDeviceNameOffset);
}

// Build a standard packet with magic header + type + device name + payload.
std::vector<uint8_t> BuildPacket(PacketType type,
                                 const std::string& device_name,
                                 const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> packet;
  packet.reserve(kPayloadOffset + payload.size());
  packet.insert(packet.end(), kProlinkHeader, kProlinkHeader + kHeaderSize);
  packet.push_back(static_cast<uint8_t>(type));

  std::array<uint8_t, kDeviceNameLength> name_bytes{};
  const size_t copy_len = std::min(device_name.size(), name_bytes.size());
  std::memcpy(name_bytes.data(), device_name.data(), copy_len);
  packet.insert(packet.end(), name_bytes.begin(), name_bytes.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

// Build a keep-alive/announce packet for port 50000 broadcast.
std::vector<uint8_t> BuildAnnouncePacket(const Config& config) {
  std::array<uint8_t, 4> ip_bytes{};
  if (!config.device_ip.empty()) {
    in_addr addr{};
    if (inet_pton(AF_INET, config.device_ip.c_str(), &addr) == 1) {
      std::memcpy(ip_bytes.data(), &addr, ip_bytes.size());
    }
  }

  std::array<uint8_t, kDeviceNameLength> name_bytes{};
  const size_t copy_len = std::min(config.device_name.size(), name_bytes.size());
  std::memcpy(name_bytes.data(), config.device_name.data(), copy_len);

  std::vector<uint8_t> packet;
  packet.reserve(0x36);
  packet.insert(packet.end(), kProlinkHeader, kProlinkHeader + kHeaderSize);
  packet.push_back(0x06);
  packet.push_back(0x00);
  packet.insert(packet.end(), name_bytes.begin(), name_bytes.end());
  packet.push_back(0x01);
  packet.push_back(0x02);
  packet.push_back(0x00);
  packet.push_back(0x36);
  packet.push_back(config.device_number);
  packet.push_back(config.device_type);
  packet.insert(packet.end(), config.mac_address.begin(), config.mac_address.end());
  packet.insert(packet.end(), ip_bytes.begin(), ip_bytes.end());
  packet.push_back(0x01);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(config.device_type);
  packet.push_back(0x00);
  return packet;
}

// Convert a string address and port into a sockaddr_in.
sockaddr_in MakeSockaddr(const std::string& address, uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (address.empty() || address == "0.0.0.0") {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
  }
  return addr;
}

// Minimal UDP socket wrapper for send/recv with broadcast support.
class UdpSocket {
 public:
  UdpSocket() = default;
  ~UdpSocket() { Close(); }

  bool Open(uint16_t port, const std::string& bind_address, bool allow_broadcast) {
    if (fd_ >= 0) {
      return true;
    }
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
      last_error_ = "socket() failed: " + std::string(std::strerror(errno));
      return false;
    }
    int reuse = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
      last_error_ = "setsockopt(SO_REUSEADDR) failed: " + std::string(std::strerror(errno));
      Close();
      return false;
    }
    if (allow_broadcast) {
      int broadcast = 1;
      if (::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        last_error_ = "setsockopt(SO_BROADCAST) failed: " + std::string(std::strerror(errno));
        Close();
        return false;
      }
    }
    sockaddr_in addr = MakeSockaddr(bind_address, port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      std::ostringstream oss;
      oss << "bind(" << bind_address << ":" << port << ") failed: "
          << std::strerror(errno);
      last_error_ = oss.str();
      Close();
      return false;
    }
    return true;
  }

  void Close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  int fd() const { return fd_; }
  const std::string& last_error() const { return last_error_; }

  ssize_t SendTo(const std::vector<uint8_t>& data, const sockaddr_in& addr) {
    return ::sendto(fd_, data.data(), data.size(), 0,
                    reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
  }

  ssize_t RecvFrom(uint8_t* buffer, size_t length, sockaddr_in* addr,
                   socklen_t* addr_len) {
    return ::recvfrom(fd_, buffer, length, 0,
                      reinterpret_cast<sockaddr*>(addr), addr_len);
  }

 private:
  int fd_ = -1;
  std::string last_error_;
};

// Snapshot of the local beat clock at a point in time.
struct BeatSnapshot {
  uint32_t beat = 1;
  uint8_t beat_within_bar = 1;
  double tempo_bpm = 120.0;
  double beat_interval_ms = 500.0;
  double bar_interval_ms = 2000.0;
  std::chrono::steady_clock::time_point beat_time;
  std::chrono::steady_clock::time_point next_beat_time;
};

// Beat clock used for scheduling outgoing beat packets.
class BeatClock {
 public:
  explicit BeatClock(int beats_per_bar)
      : beats_per_bar_(beats_per_bar),
        anchor_time_(std::chrono::steady_clock::now()) {}

  void SetTempo(double bpm) {
    tempo_bpm_ = bpm > 0.0 ? bpm : 120.0;
  }

  void SetPlaying(bool playing) { playing_ = playing; }

  void AlignToBeatNumber(uint32_t beat, uint8_t beat_within_bar,
                         std::chrono::steady_clock::time_point when) {
    anchor_time_ = when;
    anchor_beat_ = beat == 0 ? 1 : beat;
    anchor_beat_within_bar_ = beat_within_bar == 0 ? 1 : beat_within_bar;
  }

  void AlignToBeatWithinBar(uint8_t beat_within_bar,
                            std::chrono::steady_clock::time_point when) {
    if (beats_per_bar_ <= 0) {
      return;
    }
    uint8_t current = BeatWithinBar(anchor_beat_);
    int diff = static_cast<int>(beat_within_bar) - static_cast<int>(current);
    if (diff < 0) {
      diff += beats_per_bar_;
    }
    anchor_beat_ += static_cast<uint32_t>(diff);
    anchor_beat_within_bar_ = beat_within_bar == 0 ? 1 : beat_within_bar;
    anchor_time_ = when;
  }

  BeatSnapshot Snapshot(std::chrono::steady_clock::time_point now) const {
    BeatSnapshot snapshot;
    snapshot.tempo_bpm = tempo_bpm_;
    snapshot.beat_interval_ms = 60000.0 / snapshot.tempo_bpm;
    snapshot.bar_interval_ms = snapshot.beat_interval_ms * beats_per_bar_;

    if (!playing_) {
      snapshot.beat = anchor_beat_;
      snapshot.beat_within_bar = BeatWithinBar(anchor_beat_);
      snapshot.beat_time = now;
      const auto beat_duration =
          std::chrono::duration<double, std::milli>(snapshot.beat_interval_ms);
      const auto beat_duration_clock =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(beat_duration);
      snapshot.next_beat_time = now + beat_duration_clock;
      return snapshot;
    }

    const auto elapsed =
        std::chrono::duration<double, std::milli>(now - anchor_time_).count();
    const double beat_offset = elapsed / snapshot.beat_interval_ms;
    const uint32_t beat_delta =
        beat_offset < 0 ? 0 : static_cast<uint32_t>(std::floor(beat_offset));
    snapshot.beat = anchor_beat_ + beat_delta;
    snapshot.beat_within_bar = BeatWithinBar(snapshot.beat);
    const auto beat_duration =
        std::chrono::duration<double, std::milli>(snapshot.beat_interval_ms);
    const auto beat_duration_clock =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(beat_duration);
    snapshot.beat_time = anchor_time_ + beat_duration_clock * beat_delta;
    snapshot.next_beat_time = snapshot.beat_time + beat_duration_clock;
    return snapshot;
  }

 private:
  uint8_t BeatWithinBar(uint32_t beat) const {
    if (beats_per_bar_ <= 0) {
      return 1;
    }
    return static_cast<uint8_t>(((beat - 1) % beats_per_bar_) + 1);
  }

  int beats_per_bar_ = 4;
  double tempo_bpm_ = 120.0;
  bool playing_ = false;
  std::chrono::steady_clock::time_point anchor_time_;
  uint32_t anchor_beat_ = 1;
  uint8_t anchor_beat_within_bar_ = 1;
};

// Parse a beat packet (0x60 bytes) into BeatInfo.
bool ParseBeat(const uint8_t* data, size_t length, BeatInfo* out) {
  if (!out || length < kBeatPacketSize || !HasHeader(data, length)) {
    return false;
  }
  if (data[kPacketTypeOffset] != static_cast<uint8_t>(PacketType::kBeat)) {
    return false;
  }
  // Safe because we checked length >= kBeatPacketSize.
  out->device_name = ParseDeviceName(data, length);
  out->device_number = data[kOffsetDeviceNumber];
  out->pitch = ReadBe24(data, kOffsetBeatPitch);
  out->bpm = ReadBe16(data, kOffsetBeatBpm);
  out->beat_within_bar = data[kOffsetBeatWithinBar];
  // Sanitize beat_within_bar to valid range (1-8, common values are 1-4)
  if (out->beat_within_bar < 1 || out->beat_within_bar > 8) {
    out->beat_within_bar = 1;
  }
  out->next_beat_ms = ReadBe32(data, kOffsetBeatNext);
  out->next_bar_ms = ReadBe32(data, kOffsetBeatNextBar);
  return true;
}

// Parse a CDJ status packet into StatusInfo using common offsets.
bool ParseStatus(const uint8_t* data, size_t length, StatusInfo* out) {
  if (!out || length < kStatusMinimumSize || !HasHeader(data, length)) {
    return false;
  }
  if (data[kPacketTypeOffset] != static_cast<uint8_t>(PacketType::kCdjStatus)) {
    return false;
  }
  // Safe because we checked length >= kStatusMinimumSize.
  out->device_name = ParseDeviceName(data, length);
  out->device_number = data[kOffsetDeviceNumber];
  const uint16_t raw_bpm = ReadBe16(data, kOffsetStatusBpm);
  if (raw_bpm == kMaxUint16) {
    out->bpm.reset();
  } else {
    out->bpm = raw_bpm;
  }
  const uint32_t raw_beat = ReadBe32(data, kOffsetStatusBeat);
  if (raw_beat == kMaxUint32) {
    out->beat.reset();
  } else {
    out->beat = raw_beat;
  }
  out->pitch = ReadBe24(data, kOffsetStatusPitch);
  out->beat_within_bar = data[kOffsetStatusBeatWithinBar];
  // Sanitize beat_within_bar to valid range (1-8, common values are 1-4)
  if (out->beat_within_bar < 1 || out->beat_within_bar > 8) {
    out->beat_within_bar = 1;
  }
  if (length > kOffsetStatusMasterHandoff) {
    out->master_handoff_to = data[kOffsetStatusMasterHandoff];
  } else {
    out->master_handoff_to = 0xff;
  }
  const uint8_t flags = data[kOffsetStatusFlags];
  out->is_master = (flags & kStatusFlagMaster) != 0;
  out->is_synced = (flags & kStatusFlagSynced) != 0;
  out->is_playing = (flags & kStatusFlagPlaying) != 0;
  return true;
}

// Convert pitch value to a multiplier (1.0 at neutral pitch).
double PitchToMultiplier(uint32_t pitch) {
  return pitch / static_cast<double>(kNeutralPitch);
}

// Convert percent (-100..+100) into raw pitch value.
uint32_t PitchFromPercent(double percent) {
  return static_cast<uint32_t>(
      std::lround(percent * kNeutralPitch / 100.0) + kNeutralPitch);
}

// Beat payload template based on observed packets (see Analysis.tex beat packet layout).
// Fields at offsets documented above are overwritten before sending.
const std::vector<uint8_t>& BeatPayloadTemplate() {
  static const std::vector<uint8_t> payload = {
      0x01, 0x00, 0x0d, 0x00, 0x3c, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02,
      0x02, 0x10, 0x10, 0x10, 0x10, 0x04, 0x04, 0x04, 0x04, 0x20, 0x20, 0x20,
      0x20, 0x08, 0x08, 0x08, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x0b, 0x00, 0x00, 0x0d, 0x00};
  return payload;
}

// Status payload template based on observed CDJ status packets (see Analysis.tex).
// Fields at offsets documented above are overwritten before sending.
const std::vector<uint8_t>& StatusPayloadTemplate() {
  static const std::vector<uint8_t> payload = {
      0x01, 0x04, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x01, 0x00, 0x00, 0x03, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x04, 0x00, 0x00, 0x00, 0x04,
      0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x31, 0x2e, 0x34, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0xff, 0x00, 0x00, 0x10, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
      0x7f, 0xff, 0xff, 0xff, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
      0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x01, 0x00, 0x00,
      0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x07, 0x61, 0x00, 0x00, 0x06, 0x2f};
  return payload;
}

struct KeepAliveInfo {
  uint8_t device_number = 0;
  uint8_t device_type = 0;
  std::string device_name;
  std::string ip_address;
  std::array<uint8_t, 6> mac_address = {0, 0, 0, 0, 0, 0};
};

// Parse a keep-alive packet (type 0x06) from port 50000.
bool ParseKeepAlive(const uint8_t* data, size_t length, KeepAliveInfo* out) {
  if (!out || length < kKeepAlivePacketSize || !HasHeader(data, length)) {
    return false;
  }
  if (data[kPacketTypeOffset] != static_cast<uint8_t>(PacketType::kDeviceKeepAlive)) {
    return false;
  }
  out->device_name = ParseDeviceName(data, length);
  if (out->device_name.empty()) {
    out->device_name = ParseDeviceNameAt(data, length, kDeviceNameOffset + 1);
  }
  out->device_number = data[kOffsetKeepAliveDeviceNumber];
  out->device_type = data[kOffsetKeepAliveDeviceType];
  std::memcpy(out->mac_address.data(), data + kOffsetKeepAliveMac, out->mac_address.size());

  char ip_buffer[INET_ADDRSTRLEN] = {0};
  in_addr addr{};
  std::memcpy(&addr, data + kOffsetKeepAliveIp, sizeof(addr));
  if (inet_ntop(AF_INET, &addr, ip_buffer, sizeof(ip_buffer)) != nullptr) {
    out->ip_address = ip_buffer;
  }
  return true;
}

std::string AddrToString(const sockaddr_in& addr) {
  char buffer[INET_ADDRSTRLEN] = {0};
  if (inet_ntop(AF_INET, &addr.sin_addr, buffer, sizeof(buffer)) != nullptr) {
    return buffer;
  }
  return {};
}

void LogError(const std::string& message, const Config* config) {
  if (config && config->log_callback) {
    config->log_callback(message);
    return;
  }
  std::cerr << "[prolink] " << message << std::endl;
}

void LogCallbackError(const char* name, const Config* config) {
  std::string message = "callback threw exception: ";
  message += name;
  LogError(message, config);
}

void LogSendError(const char* packet_type, ssize_t result, size_t expected,
                  const Config* config) {
  std::ostringstream oss;
  if (result < 0) {
    oss << "Failed to send " << packet_type << " packet: "
        << std::strerror(errno);
  } else if (static_cast<size_t>(result) != expected) {
    oss << "Partial send of " << packet_type << " packet: " << result
        << " of " << expected << " bytes";
  } else {
    return;
  }
  LogError(oss.str(), config);
}

}  // namespace

bool Config::Validate(std::string* error) const {
  auto fail = [&](const std::string& message) {
    if (error) {
      *error = message;
    }
    return false;
  };
  auto is_valid_ipv4 = [](const std::string& addr) {
    if (addr.empty()) {
      return false;
    }
    in_addr parsed{};
    return inet_pton(AF_INET, addr.c_str(), &parsed) == 1;
  };
  if (device_name.empty()) {
    return fail("device_name must not be empty");
  }
  if (device_number == 0) {
    return fail("device_number must be non-zero");
  }
  if (status_interval_ms <= 0 || announce_interval_ms <= 0 || beats_per_bar <= 0) {
    return fail("intervals and beats_per_bar must be positive");
  }
  if (device_timeout.count() <= 0 || device_prune_interval.count() <= 0) {
    return fail("device timeouts must be positive");
  }
  if (master_request_retry_interval.count() <= 0 ||
      master_request_timeout.count() <= 0 ||
      master_request_max_retries <= 0) {
    return fail("master request policy must be positive");
  }
  if (master_request_timeout < master_request_retry_interval) {
    return fail("master_request_timeout must be >= master_request_retry_interval");
  }
  if (!device_ip.empty()) {
    if (!is_valid_ipv4(device_ip)) {
      return fail("device_ip must be a valid IPv4 address");
    }
  }
  if (!bind_address.empty() && bind_address != "0.0.0.0") {
    if (!is_valid_ipv4(bind_address)) {
      return fail("bind_address must be a valid IPv4 address");
    }
  }
  if (!broadcast_address.empty() && !is_valid_ipv4(broadcast_address)) {
    return fail("broadcast_address must be a valid IPv4 address");
  }
  if (!announce_address.empty() && !is_valid_ipv4(announce_address)) {
    return fail("announce_address must be a valid IPv4 address");
  }
  if (!capture_file.empty() && !replay_file.empty()) {
    return fail("capture_file and replay_file are mutually exclusive");
  }
  return true;
}

double BeatInfo::effective_bpm() const {
  return bpm * PitchToMultiplier(pitch) / 100.0;
}

std::optional<double> StatusInfo::effective_bpm() const {
  if (!bpm.has_value()) {
    return std::nullopt;
  }
  return bpm.value() * PitchToMultiplier(pitch) / 100.0;
}

struct SessionMetricsAtomic {
  std::atomic<uint64_t> packets_received{0};
  std::atomic<uint64_t> packets_sent{0};
  std::atomic<uint64_t> parse_errors{0};
  std::atomic<uint64_t> send_errors{0};
  std::atomic<uint64_t> callback_exceptions{0};

  SessionMetrics Snapshot() const {
    SessionMetrics snapshot;
    snapshot.packets_received = packets_received.load();
    snapshot.packets_sent = packets_sent.load();
    snapshot.parse_errors = parse_errors.load();
    snapshot.send_errors = send_errors.load();
    snapshot.callback_exceptions = callback_exceptions.load();
    return snapshot;
  }
};

struct Session::Impl {
#ifdef PROLINK_TESTING
  friend void test::InjectKeepAlive(Session& session,
                                    uint8_t device_number,
                                    uint8_t device_type,
                                    const std::string& device_name,
                                    const std::string& ip_address,
                                    const std::array<uint8_t, 6>& mac_address);
  friend void test::SetDeviceLastSeen(Session& session,
                                      uint8_t device_number,
                                      std::chrono::steady_clock::time_point when);
  friend void test::PruneDevices(Session& session,
                                 std::chrono::steady_clock::time_point now);
  friend size_t test::GetDeviceRecordCount(Session& session);
#endif

  explicit Impl(Config config)
      : config_(std::move(config)),
        clock_(config_.beats_per_bar) {
    state_.tempo_bpm = config_.tempo_bpm;
    state_.pitch = PitchFromPercent(config_.pitch_percent);
    state_.playing = config_.playing;
    state_.master = config_.master;
    state_.synced = config_.synced;
    clock_.SetTempo(state_.tempo_bpm);
    clock_.SetPlaying(state_.playing);
  }

  bool Start() {
    if (running_.exchange(true)) {
      return true;
    }
    start_error_.clear();
    std::string error;
    if (!config_.Validate(&error)) {
      start_error_ = error;
      LogError(error, &config_);
      running_ = false;
      return false;
    }
    replay_mode_ = !config_.replay_file.empty();
    if (!config_.replay_file.empty()) {
      replay_stream_.open(config_.replay_file, std::ios::binary | std::ios::in);
      if (!replay_stream_) {
        start_error_ = "failed to open replay file: " + config_.replay_file;
        LogError(start_error_, &config_);
        running_ = false;
        return false;
      }
    }
    if (!config_.capture_file.empty()) {
      capture_stream_.open(config_.capture_file,
                           std::ios::binary | std::ios::out | std::ios::trunc);
      if (!capture_stream_) {
        start_error_ = "failed to open capture file: " + config_.capture_file;
        LogError(start_error_, &config_);
        running_ = false;
        replay_stream_.close();
        return false;
      }
    }
    const uint16_t beat_port = replay_mode_ ? 0 : kBeatPort;
    const uint16_t status_port = replay_mode_ ? 0 : kStatusPort;
    if (!beat_socket_.Open(beat_port, config_.bind_address, true)) {
      start_error_ = beat_socket_.last_error();
      LogError(start_error_, &config_);
      running_ = false;
      capture_stream_.close();
      replay_stream_.close();
      return false;
    }
    if (!status_socket_.Open(status_port, config_.bind_address, true)) {
      start_error_ = status_socket_.last_error();
      LogError(start_error_, &config_);
      beat_socket_.Close();
      running_ = false;
      capture_stream_.close();
      replay_stream_.close();
      return false;
    }
    if (!replay_mode_) {
      if (!device_socket_.Open(kAnnouncePort, config_.bind_address, true)) {
        start_error_ = device_socket_.last_error();
        LogError(start_error_, &config_);
        status_socket_.Close();
        beat_socket_.Close();
        running_ = false;
        capture_stream_.close();
        replay_stream_.close();
        return false;
      }
    }
    if (!announce_socket_.Open(0, config_.bind_address, true)) {
      start_error_ = announce_socket_.last_error();
      LogError(start_error_, &config_);
      device_socket_.Close();
      status_socket_.Close();
      beat_socket_.Close();
      running_ = false;
      capture_stream_.close();
      replay_stream_.close();
      return false;
    }
    try {
      recv_thread_ = std::thread([this]() { RecvLoop(); });
      beat_thread_ = std::thread([this]() { BeatLoop(); });
      status_thread_ = std::thread([this]() { StatusLoop(); });
      if (config_.send_announces) {
        announce_thread_ = std::thread([this]() { AnnounceLoop(); });
      }
      prune_thread_ = std::thread([this]() { PruneLoop(); });
    } catch (const std::exception& ex) {
      start_error_ = std::string("thread start failed: ") + ex.what();
      LogError(start_error_, &config_);
      running_ = false;
      Stop();
      return false;
    } catch (...) {
      start_error_ = "thread start failed: unknown error";
      LogError(start_error_, &config_);
      running_ = false;
      Stop();
      return false;
    }
    return true;
  }

  void Stop() {
    if (!running_.exchange(false)) {
      return;
    }
    state_cv_.notify_all();
    beat_socket_.Close();
    status_socket_.Close();
    device_socket_.Close();
    announce_socket_.Close();
    if (recv_thread_.joinable()) {
      recv_thread_.join();
    }
    if (beat_thread_.joinable()) {
      beat_thread_.join();
    }
    if (status_thread_.joinable()) {
      status_thread_.join();
    }
    if (announce_thread_.joinable()) {
      announce_thread_.join();
    }
    if (prune_thread_.joinable()) {
      prune_thread_.join();
    }
    {
      std::lock_guard<std::mutex> lock(capture_mutex_);
      if (capture_stream_.is_open()) {
        capture_stream_.close();
      }
    }
    if (replay_stream_.is_open()) {
      replay_stream_.close();
    }
  }

  void SetBeatCallback(BeatCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    beat_cb_ = std::move(cb);
  }
  void SetStatusCallback(StatusCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    status_cb_ = std::move(cb);
  }
  void SetDeviceCallback(DeviceCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    device_cb_ = std::move(cb);
  }
  void SetDeviceEventCallback(DeviceEventCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    device_event_cb_ = std::move(cb);
  }

  void SetTempo(double bpm) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.tempo_bpm = bpm;
    clock_.SetTempo(bpm);
    state_cv_.notify_all();
  }

  void SetPitchPercent(double percent) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.pitch = PitchFromPercent(percent);
  }

  void SetPlaying(bool playing) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.playing = playing;
    clock_.SetPlaying(playing);
    if (playing) {
      last_sent_beat_ = 0;
    }
    state_cv_.notify_all();
  }

  void SetMaster(bool master) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.master = master;
    if (!master) {
      handoff_to_device_ = 0xff;
    }
  }

  void SetSynced(bool synced) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.synced = synced;
  }

  void SetBeat(uint32_t beat, uint8_t beat_within_bar) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const auto now = std::chrono::steady_clock::now();
    state_.beat = beat;
    state_.beat_within_bar = beat_within_bar;
    clock_.AlignToBeatNumber(beat, beat_within_bar, now);
    last_sent_beat_ = 0;
    state_cv_.notify_all();
  }

  void SendBeat() { SendBeatInternal(); }
  void SendStatus() { SendStatusInternal(); }
  void SendSyncControl(uint8_t target_device, SyncCommand command) {
    SendSyncControlInternal(target_device, command);
  }
  void RequestMasterRole() { RequestMasterRoleInternal(); }
  void SendMasterHandoffRequest(uint8_t target_device) {
    SendMasterHandoffRequestInternal(target_device);
  }

  std::optional<StatusInfo> GetTempoMaster() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return master_status_;
  }

  std::vector<DeviceInfo> GetDevices() const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    std::vector<DeviceInfo> result;
    result.reserve(devices_.size());
    for (const auto& entry : devices_) {
      if (entry.second.active) {
        result.push_back(entry.second.info);
      }
    }
    return result;
  }

  std::string GetLastError() const {
    return start_error_;
  }

  SessionMetrics GetMetrics() const {
    return metrics_.Snapshot();
  }

 private:
  struct DeviceRecord {
    DeviceInfo info;
    bool active = false;
  };

  struct State {
    double tempo_bpm = 120.0;
    uint32_t pitch = kNeutralPitch;
    bool playing = false;
    bool master = false;
    bool synced = false;
    uint32_t beat = 1;
    uint8_t beat_within_bar = 1;
  };

  void RecordCallbackException(const char* name) {
    metrics_.callback_exceptions.fetch_add(1);
    LogCallbackError(name, &config_);
  }

  void RecordSendResult(const char* packet_type, ssize_t result, size_t expected) {
    if (result < 0 || static_cast<size_t>(result) != expected) {
      metrics_.send_errors.fetch_add(1);
      LogSendError(packet_type, result, expected, &config_);
      return;
    }
    metrics_.packets_sent.fetch_add(1);
  }

  void RecordParseError() {
    metrics_.parse_errors.fetch_add(1);
  }

  void RecordPacketReceived() {
    metrics_.packets_received.fetch_add(1);
  }

  void CapturePacket(const uint8_t* data, size_t length) {
    if (!capture_stream_.is_open()) {
      return;
    }
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const uint64_t timestamp_us =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    const uint32_t length_u32 = static_cast<uint32_t>(length);
    std::lock_guard<std::mutex> lock(capture_mutex_);
    capture_stream_.write(reinterpret_cast<const char*>(&timestamp_us),
                          sizeof(timestamp_us));
    capture_stream_.write(reinterpret_cast<const char*>(&length_u32),
                          sizeof(length_u32));
    capture_stream_.write(reinterpret_cast<const char*>(data),
                          static_cast<std::streamsize>(length));
  }

  bool ReadReplayPacket(uint64_t* timestamp_us, std::vector<uint8_t>* packet) {
    if (!replay_stream_.is_open()) {
      return false;
    }
    uint64_t ts = 0;
    uint32_t length = 0;
    replay_stream_.read(reinterpret_cast<char*>(&ts), sizeof(ts));
    if (!replay_stream_) {
      return false;
    }
    replay_stream_.read(reinterpret_cast<char*>(&length), sizeof(length));
    if (!replay_stream_) {
      return false;
    }
    if (length > kMaxReplayPacketSize) {
      LogError("Replay packet too large, aborting", &config_);
      return false;
    }
    std::vector<uint8_t> data(length);
    if (length > 0) {
      replay_stream_.read(reinterpret_cast<char*>(data.data()),
                          static_cast<std::streamsize>(length));
      if (!replay_stream_) {
        return false;
      }
    }
    if (timestamp_us) {
      *timestamp_us = ts;
    }
    if (packet) {
      *packet = std::move(data);
    }
    return true;
  }

  void ProcessPacket(const uint8_t* data, size_t length,
                     const std::string& addr_string) {
    if (length < kHeaderSize) {
      RecordParseError();
      return;
    }
    if (!HasHeader(data, length)) {
      RecordParseError();
      return;
    }
    if (length <= kPacketTypeOffset) {
      RecordParseError();
      return;
    }
    RecordPacketReceived();
    const uint8_t type = data[kPacketTypeOffset];
    switch (type) {
      case static_cast<uint8_t>(PacketType::kBeat): {
        BeatInfo info;
        if (!ParseBeat(data, length, &info)) {
          RecordParseError();
          return;
        }
        UpdateDeviceSeen(info.device_number, info.device_name, addr_string);
        HandleBeat(info);
        return;
      }
      case static_cast<uint8_t>(PacketType::kCdjStatus): {
        StatusInfo info;
        if (!ParseStatus(data, length, &info)) {
          RecordParseError();
          return;
        }
        UpdateDeviceSeen(info.device_number, info.device_name, addr_string);
        HandleStatus(info);
        return;
      }
      case static_cast<uint8_t>(PacketType::kSyncControl): {
        if (length <= kOffsetMasterHandoffAccepted ||
            length <= kOffsetDeviceNumber) {
          RecordParseError();
          return;
        }
        const uint8_t device_number = data[kOffsetDeviceNumber];
        UpdateDeviceSeen(device_number, ParseDeviceName(data, length), addr_string);
        HandleSyncControl(device_number, data[kOffsetMasterHandoffAccepted]);
        return;
      }
      case static_cast<uint8_t>(PacketType::kMasterHandoffRequest): {
        if (length <= kOffsetDeviceNumber) {
          RecordParseError();
          return;
        }
        const uint8_t device_number = data[kOffsetDeviceNumber];
        UpdateDeviceSeen(device_number, ParseDeviceName(data, length), addr_string);
        HandleMasterHandoffRequest(device_number);
        return;
      }
      case static_cast<uint8_t>(PacketType::kMasterHandoffResponse): {
        if (length <= kOffsetMasterHandoffAccepted ||
            length <= kOffsetDeviceNumber) {
          RecordParseError();
          return;
        }
        const uint8_t device_number = data[kOffsetDeviceNumber];
        UpdateDeviceSeen(device_number, ParseDeviceName(data, length), addr_string);
        HandleMasterHandoffResponse(device_number,
                                    data[kOffsetMasterHandoffAccepted] == 0x01);
        return;
      }
      case static_cast<uint8_t>(PacketType::kDeviceKeepAlive): {
        KeepAliveInfo info;
        if (!ParseKeepAlive(data, length, &info)) {
          RecordParseError();
          return;
        }
        UpdateDeviceFromKeepAlive(info);
        return;
      }
      default:
        return;
    }
  }

  // Receive loop for beat/status sockets.
  void RecvLoop() {
    if (replay_mode_) {
      ReplayLoop();
      return;
    }
    std::array<uint8_t, 512> buffer{};
    while (running_) {
      fd_set readfds;
      FD_ZERO(&readfds);
      int max_fd = -1;
      const int beat_fd = beat_socket_.fd();
      const int status_fd = status_socket_.fd();
      const int device_fd = device_socket_.fd();
      if (beat_fd >= 0) {
        FD_SET(beat_fd, &readfds);
        max_fd = std::max(max_fd, beat_fd);
      }
      if (status_fd >= 0) {
        FD_SET(status_fd, &readfds);
        max_fd = std::max(max_fd, status_fd);
      }
      if (device_fd >= 0) {
        FD_SET(device_fd, &readfds);
        max_fd = std::max(max_fd, device_fd);
      }
      if (max_fd < 0) {
        if (!running_) {
          return;
        }
        LogError("RecvLoop: no valid sockets, stopping", &config_);
        running_ = false;
        return;
      }
      timeval tv{};
      tv.tv_sec = 0;
      tv.tv_usec = 200000;
      const int ready = ::select(max_fd + 1, &readfds, nullptr, nullptr, &tv);
      if (ready <= 0) {
        continue;
      }
      if (beat_fd >= 0 && FD_ISSET(beat_fd, &readfds)) {
        sockaddr_in addr{};
        socklen_t addr_len = sizeof(addr);
        const ssize_t bytes = beat_socket_.RecvFrom(buffer.data(), buffer.size(),
                                                    &addr, &addr_len);
        if (bytes > 0) {
          const size_t length = static_cast<size_t>(bytes);
          CapturePacket(buffer.data(), length);
          ProcessPacket(buffer.data(), length, AddrToString(addr));
        }
      }
      if (status_fd >= 0 && FD_ISSET(status_fd, &readfds)) {
        sockaddr_in addr{};
        socklen_t addr_len = sizeof(addr);
        const ssize_t bytes = status_socket_.RecvFrom(buffer.data(), buffer.size(),
                                                      &addr, &addr_len);
        if (bytes > 0) {
          const size_t length = static_cast<size_t>(bytes);
          CapturePacket(buffer.data(), length);
          ProcessPacket(buffer.data(), length, AddrToString(addr));
        }
      }
      if (device_fd >= 0 && FD_ISSET(device_fd, &readfds)) {
        sockaddr_in addr{};
        socklen_t addr_len = sizeof(addr);
        const ssize_t bytes = device_socket_.RecvFrom(buffer.data(), buffer.size(),
                                                      &addr, &addr_len);
        if (bytes > 0) {
          const size_t length = static_cast<size_t>(bytes);
          CapturePacket(buffer.data(), length);
          ProcessPacket(buffer.data(), length, AddrToString(addr));
        }
      }
    }
  }

  void ReplayLoop() {
    uint64_t last_timestamp = 0;
    while (running_) {
      uint64_t timestamp = 0;
      std::vector<uint8_t> packet;
      if (!ReadReplayPacket(&timestamp, &packet)) {
        LogError("Replay file exhausted, stopping", &config_);
        running_ = false;
        return;
      }
      if (last_timestamp != 0 && timestamp >= last_timestamp) {
        const uint64_t delta_us = timestamp - last_timestamp;
        std::this_thread::sleep_for(std::chrono::microseconds(delta_us));
      }
      last_timestamp = timestamp;
      ProcessPacket(packet.data(), packet.size(), {});
    }
  }

  // Handle an incoming beat packet (optional follow-master alignment).
  void HandleBeat(const BeatInfo& info) {
    BeatCallback cb_copy;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      cb_copy = beat_cb_;
    }
    if (cb_copy) {
      try {
        cb_copy(info);
      } catch (...) {
        RecordCallbackException("BeatCallback");
      }
    }
    if (!config_.follow_master) {
      return;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (master_device_number_ != 0 &&
        info.device_number == master_device_number_) {
      const auto now = std::chrono::steady_clock::now();
      if (master_beat_number_ != 0) {
        master_beat_number_ += 1;
        clock_.AlignToBeatNumber(master_beat_number_, info.beat_within_bar, now);
        last_sent_beat_ = 0;
      } else {
        clock_.AlignToBeatWithinBar(info.beat_within_bar, now);
        last_sent_beat_ = 0;
      }
    }
  }

  // Handle an incoming status packet (updates tempo master state).
  void HandleStatus(const StatusInfo& info) {
    StatusCallback cb_copy;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      cb_copy = status_cb_;
    }
    if (cb_copy) {
      try {
        cb_copy(info);
      } catch (...) {
        RecordCallbackException("StatusCallback");
      }
    }
    bool should_request_new_master = false;
    uint8_t request_target = 0;
    if (info.is_master) {
      const auto now = std::chrono::steady_clock::now();
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (requesting_master_from_ != 0 &&
          requesting_master_from_ != info.device_number) {
        if (info.device_number == config_.device_number) {
          requesting_master_from_ = 0;
          master_request_attempts_ = 0;
          master_request_time_ = std::chrono::steady_clock::time_point{};
          master_request_start_time_ = std::chrono::steady_clock::time_point{};
        } else {
          LogError("Master changed during handoff request, restarting", &config_);
          requesting_master_from_ = info.device_number;
          master_request_attempts_ = 1;
          master_request_time_ = now;
          master_request_start_time_ = now;
          request_target = info.device_number;
          should_request_new_master = true;
        }
      }
      master_status_ = info;
      master_device_number_ = info.device_number;
      if (info.beat.has_value()) {
        master_beat_number_ = info.beat.value();
      }
      if (config_.follow_master && info.bpm.has_value() && info.beat.has_value()) {
        const double bpm = info.bpm.value() / 100.0;
        state_.tempo_bpm = bpm;
        clock_.SetTempo(bpm);
        clock_.AlignToBeatNumber(info.beat.value(), info.beat_within_bar, now);
        state_.synced = true;
        last_sent_beat_ = 0;
      }
    }
    if (should_request_new_master) {
      SendMasterHandoffRequestInternal(request_target);
    }
    if (info.master_handoff_to == config_.device_number) {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.master = true;
      state_.synced = true;
      last_sent_beat_ = 0;
      requesting_master_from_ = 0;
      master_request_time_ = std::chrono::steady_clock::time_point{};
      master_request_attempts_ = 0;
      master_request_start_time_ = std::chrono::steady_clock::time_point{};
    }
    if (handoff_to_device_ != 0xff && info.device_number == handoff_to_device_ &&
        info.is_master) {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.master = false;
      handoff_to_device_ = 0xff;
      master_request_time_ = std::chrono::steady_clock::time_point{};
      master_request_attempts_ = 0;
      master_request_start_time_ = std::chrono::steady_clock::time_point{};
    }
  }

  // Schedule beat packets based on the local beat clock.
  void BeatLoop() {
    while (running_) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      state_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
        return !running_ || (config_.send_beats && state_.playing);
      });
      if (!running_) {
        return;
      }
      BeatSnapshot snapshot = clock_.Snapshot(std::chrono::steady_clock::now());
      const auto next_time = snapshot.next_beat_time;
      lock.unlock();
      std::this_thread::sleep_until(next_time);
      if (!running_) {
        return;
      }
      SendBeatInternal();
    }
  }

  // Send status packets at a fixed interval.
  void StatusLoop() {
    while (running_) {
      if (config_.send_status) {
        SendStatusInternal();
      }
      MaybeRetryMasterRequest();
      std::this_thread::sleep_for(
          std::chrono::milliseconds(config_.status_interval_ms));
    }
  }

  // Periodically broadcast keep-alive packets on port 50000.
  void AnnounceLoop() {
    if (config_.device_ip.empty()) {
      return;
    }
    const auto packet = BuildAnnouncePacket(config_);
    const auto addr = MakeSockaddr(config_.announce_address, kAnnouncePort);
    while (running_) {
      const ssize_t result = announce_socket_.SendTo(packet, addr);
      RecordSendResult("announce", result, packet.size());
      std::this_thread::sleep_for(
          std::chrono::milliseconds(config_.announce_interval_ms));
    }
  }

  // Remove devices that have not been seen within the timeout.
  void PruneLoop() {
    while (running_) {
      std::this_thread::sleep_for(config_.device_prune_interval);
      if (!running_) {
        return;
      }
      RunPrune(std::chrono::steady_clock::now());
    }
  }

  void RunPrune(std::chrono::steady_clock::time_point now) {
    std::vector<DeviceInfo> expired;
    {
      std::lock_guard<std::mutex> lock(devices_mutex_);
      for (auto& entry : devices_) {
        if (entry.second.active &&
            now - entry.second.info.last_seen > config_.device_timeout) {
          entry.second.active = false;
          expired.push_back(entry.second.info);
        }
      }

      const auto remove_after = config_.device_timeout * 10;
      auto it = devices_.begin();
      while (it != devices_.end()) {
        if (!it->second.active &&
            now - it->second.info.last_seen > remove_after) {
          it = devices_.erase(it);
        } else {
          ++it;
        }
      }
    }
    DeviceEventCallback dev_event_cb_copy;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      dev_event_cb_copy = device_event_cb_;
    }
    if (dev_event_cb_copy) {
      for (const auto& device : expired) {
        try {
          dev_event_cb_copy({DeviceEventType::kExpired, device});
        } catch (...) {
          RecordCallbackException("DeviceEventCallback");
        }
      }
    }
  }

  // Build and broadcast a beat packet.
  void SendBeatInternal() {
    if (!config_.send_beats) {
      return;
    }
    BeatSnapshot snapshot;
    uint32_t pitch = kNeutralPitch;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!state_.playing) {
        return;
      }
      snapshot = clock_.Snapshot(std::chrono::steady_clock::now());
      pitch = state_.pitch;
      if (snapshot.beat == last_sent_beat_) {
        return;
      }
      last_sent_beat_ = snapshot.beat;
    }

    std::vector<uint8_t> payload = BeatPayloadTemplate();
    assert(payload.size() >= kOffsetBeatPayloadDeviceNumber2 + 1);
    payload[kOffsetBeatPayloadDeviceNumber] = config_.device_number;

    const uint32_t beat_interval = static_cast<uint32_t>(snapshot.beat_interval_ms);
    const uint32_t bar_interval = static_cast<uint32_t>(snapshot.bar_interval_ms);
    WriteBe32(payload, kOffsetBeatPayloadInterval, beat_interval);
    WriteBe32(payload, kOffsetBeatPayloadInterval2, SafeMul(beat_interval, 2));
    WriteBe32(payload, kOffsetBeatPayloadInterval4, SafeMul(beat_interval, 4));
    WriteBe32(payload, kOffsetBeatPayloadInterval8, SafeMul(beat_interval, 8));

    const int beats_left = config_.beats_per_bar + 1 - snapshot.beat_within_bar;
    const uint32_t next_bar = SafeMul(beat_interval, static_cast<uint32_t>(beats_left));
    WriteBe32(payload, kOffsetBeatPayloadNextBar, next_bar);
    WriteBe32(payload, kOffsetBeatPayloadNextBar2, next_bar + bar_interval);

    WriteBe24(payload, kOffsetBeatPayloadPitch, pitch);
    WriteBe16(payload, kOffsetBeatPayloadBpm,
              static_cast<uint32_t>(std::lround(snapshot.tempo_bpm * 100)));
    payload[kOffsetBeatPayloadBeatWithinBar] = snapshot.beat_within_bar;
    payload[kOffsetBeatPayloadDeviceNumber2] = config_.device_number;

    const auto packet = BuildPacket(PacketType::kBeat, config_.device_name, payload);
    const auto addr = MakeSockaddr(config_.broadcast_address, kBeatPort);
    const ssize_t result = beat_socket_.SendTo(packet, addr);
    RecordSendResult("beat", result, packet.size());
  }

  // Build and broadcast a CDJ status packet.
  void SendStatusInternal() {
    if (!config_.send_status) {
      return;
    }
    State snapshot_state;
    BeatSnapshot beat_snapshot;
    uint32_t packet_counter = 0;
    uint8_t handoff_to_device = 0xff;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      snapshot_state = state_;
      beat_snapshot = clock_.Snapshot(std::chrono::steady_clock::now());
      packet_counter = ++packet_counter_;
      handoff_to_device = handoff_to_device_;
    }

    std::vector<uint8_t> payload = StatusPayloadTemplate();
    assert(payload.size() >= kOffsetStatusPayloadPacketCounter + 4);
    payload[kOffsetStatusPayloadDeviceNumber] = config_.device_number;
    payload[kOffsetStatusPayloadDeviceNumber2] = config_.device_number;
    payload[kOffsetStatusPayloadPlayingFlag] = snapshot_state.playing ? 1 : 0;
    payload[kOffsetStatusPayloadDeviceNumber3] = config_.device_number;
    payload[kOffsetStatusPayloadPlayState] = snapshot_state.playing ? 3 : 5;
    payload[kOffsetStatusPayloadFlagByte] = static_cast<uint8_t>(0x84 +
        (snapshot_state.playing ? 0x40 : 0) +
        (snapshot_state.master ? 0x20 : 0) +
        (snapshot_state.synced ? 0x10 : 0));
    payload[kOffsetStatusPayloadPlayState2] = snapshot_state.playing ? 0x7a : 0x7e;
    payload[kOffsetStatusPayloadPlayState3] = snapshot_state.playing ? 9 : 1;
    payload[kOffsetStatusPayloadMasterFlag] = snapshot_state.master ? 1 : 0;
    payload[kOffsetStatusPayloadMasterHandoff] =
        snapshot_state.master ? handoff_to_device : 0xff;

    WriteBe24(payload, kOffsetStatusPayloadPitch, snapshot_state.pitch);
    WriteBe16(payload, kOffsetStatusPayloadBpm,
              static_cast<uint32_t>(std::lround(snapshot_state.tempo_bpm * 100)));
    WriteBe32(payload, kOffsetStatusPayloadBeatNumber, beat_snapshot.beat);
    payload[kOffsetStatusPayloadBeatWithinBar] = beat_snapshot.beat_within_bar;
    WriteBe32(payload, kOffsetStatusPayloadPacketCounter, packet_counter);

    const auto packet =
        BuildPacket(PacketType::kCdjStatus, config_.device_name, payload);
    const auto addr = MakeSockaddr(config_.broadcast_address, kStatusPort);
    const ssize_t result = status_socket_.SendTo(packet, addr);
    RecordSendResult("status", result, packet.size());
  }

  // Update or create a device record from keep-alive packets.
  void UpdateDeviceFromKeepAlive(const KeepAliveInfo& info) {
    const auto now = std::chrono::steady_clock::now();
    DeviceInfo snapshot;
    DeviceEventType event_type = DeviceEventType::kSeen;
    bool should_notify = false;
    {
      std::lock_guard<std::mutex> lock(devices_mutex_);
      auto& record = devices_[info.device_number];
      const bool was_active = record.active;
      if (record.info.device_number == 0) {
        record.info.device_number = info.device_number;
        should_notify = true;
        event_type = DeviceEventType::kSeen;
      }
      if (record.info.device_type != info.device_type) {
        record.info.device_type = info.device_type;
        should_notify = true;
        event_type = DeviceEventType::kUpdated;
      }
      if (!info.device_name.empty() && record.info.device_name != info.device_name) {
        record.info.device_name = info.device_name;
        should_notify = true;
        event_type = DeviceEventType::kUpdated;
      }
      if (!info.ip_address.empty() && record.info.ip_address != info.ip_address) {
        record.info.ip_address = info.ip_address;
        should_notify = true;
        event_type = DeviceEventType::kUpdated;
      }
      if (record.info.mac_address != info.mac_address) {
        record.info.mac_address = info.mac_address;
        should_notify = true;
        event_type = DeviceEventType::kUpdated;
      }
      record.info.last_seen = now;
      if (!record.active) {
        record.active = true;
        should_notify = true;
        event_type = DeviceEventType::kSeen;
      }
      if (record.active && was_active && should_notify &&
          event_type != DeviceEventType::kUpdated) {
        event_type = DeviceEventType::kUpdated;
      }
      snapshot = record.info;
    }
    if (should_notify) {
      DeviceCallback dev_cb_copy;
      DeviceEventCallback dev_event_cb_copy;
      {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        dev_cb_copy = device_cb_;
        dev_event_cb_copy = device_event_cb_;
      }
      if (dev_cb_copy) {
        try {
          dev_cb_copy(snapshot);
        } catch (...) {
          RecordCallbackException("DeviceCallback");
        }
      }
      if (dev_event_cb_copy) {
        try {
          dev_event_cb_copy({event_type, snapshot});
        } catch (...) {
          RecordCallbackException("DeviceEventCallback");
        }
      }
    }
  }

  // Update device last-seen from beat/status packets.
  void UpdateDeviceSeen(uint8_t device_number, const std::string& name,
                        const std::string& ip) {
    if (device_number == 0) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    DeviceInfo snapshot;
    DeviceEventType event_type = DeviceEventType::kSeen;
    bool should_notify = false;
    {
      std::lock_guard<std::mutex> lock(devices_mutex_);
      auto& record = devices_[device_number];
      const bool was_active = record.active;
      if (record.info.device_number == 0) {
        record.info.device_number = device_number;
        should_notify = true;
        event_type = DeviceEventType::kSeen;
      }
      if (!name.empty() && record.info.device_name != name) {
        record.info.device_name = name;
        should_notify = true;
        event_type = DeviceEventType::kUpdated;
      }
      if (!ip.empty() && record.info.ip_address != ip) {
        record.info.ip_address = ip;
        should_notify = true;
        event_type = DeviceEventType::kUpdated;
      }
      record.info.last_seen = now;
      if (!record.active) {
        record.active = true;
        should_notify = true;
        event_type = DeviceEventType::kSeen;
      }
      if (record.active && was_active && should_notify &&
          event_type != DeviceEventType::kUpdated) {
        event_type = DeviceEventType::kUpdated;
      }
      snapshot = record.info;
    }
    if (should_notify) {
      DeviceCallback dev_cb_copy;
      DeviceEventCallback dev_event_cb_copy;
      {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        dev_cb_copy = device_cb_;
        dev_event_cb_copy = device_event_cb_;
      }
      if (dev_cb_copy) {
        try {
          dev_cb_copy(snapshot);
        } catch (...) {
          RecordCallbackException("DeviceCallback");
        }
      }
      if (dev_event_cb_copy) {
        try {
          dev_event_cb_copy({event_type, snapshot});
        } catch (...) {
          RecordCallbackException("DeviceEventCallback");
        }
      }
    }
  }

  // Find the IP address for a device number, if known.
  std::optional<std::string> LookupDeviceIp(uint8_t device_number) const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    auto it = devices_.find(device_number);
    if (it == devices_.end() || it->second.info.ip_address.empty()) {
      return std::nullopt;
    }
    return it->second.info.ip_address;
  }

  // Build and send a sync control packet to a device.
  void SendSyncControlInternal(uint8_t target_device, SyncCommand command) {
    std::vector<uint8_t> payload(kControlPayloadSize, 0x00);
    payload[0x00] = 0x01;
    payload[0x01] = 0x00;
    payload[kControlPayloadDeviceNumber] = config_.device_number;
    payload[0x03] = 0x00;
    payload[0x04] = 0x08;
    payload[kControlPayloadSender] = config_.device_number;
    payload[kControlPayloadCommand] = static_cast<uint8_t>(command);

    const auto packet = BuildPacket(PacketType::kSyncControl, config_.device_name, payload);
    const auto target_ip = LookupDeviceIp(target_device);
    const auto addr = MakeSockaddr(target_ip.value_or(config_.broadcast_address),
                                   kBeatPort);
    const ssize_t result = beat_socket_.SendTo(packet, addr);
    RecordSendResult("sync_control", result, packet.size());
  }

  // Send a master handoff request to the current tempo master.
  void SendMasterHandoffRequestInternal(uint8_t target_device) {
    std::vector<uint8_t> payload(kHandoffRequestPayloadSize, 0x00);
    payload[0x00] = 0x01;
    payload[0x01] = 0x00;
    payload[kControlPayloadDeviceNumber] = config_.device_number;
    payload[0x03] = 0x00;
    payload[0x04] = 0x04;
    payload[kControlPayloadSender] = config_.device_number;

    const auto packet =
        BuildPacket(PacketType::kMasterHandoffRequest, config_.device_name, payload);
    const auto target_ip = LookupDeviceIp(target_device);
    const auto addr = MakeSockaddr(target_ip.value_or(config_.broadcast_address),
                                   kBeatPort);
    const ssize_t result = beat_socket_.SendTo(packet, addr);
    RecordSendResult("master_handoff_request", result, packet.size());
  }

  // Retry master handoff requests with timeout and retry budget.
  void MaybeRetryMasterRequest() {
    const auto now = std::chrono::steady_clock::now();
    uint8_t target_device = 0;
    bool should_send = false;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (requesting_master_from_ == 0) {
        return;
      }
      const auto start_time =
          master_request_start_time_.time_since_epoch().count() == 0
              ? master_request_time_
              : master_request_start_time_;
      if (now - start_time >= config_.master_request_timeout) {
        requesting_master_from_ = 0;
        master_request_attempts_ = 0;
        master_request_time_ = std::chrono::steady_clock::time_point{};
        master_request_start_time_ = std::chrono::steady_clock::time_point{};
        return;
      }
      const int max_retries = std::max(1, config_.master_request_max_retries);
      if (master_request_attempts_ >= max_retries) {
        return;
      }
      if (now - master_request_time_ >= config_.master_request_retry_interval) {
        master_request_time_ = now;
        master_request_attempts_ += 1;
        target_device = requesting_master_from_;
        should_send = true;
      }
    }
    if (should_send) {
      SendMasterHandoffRequestInternal(target_device);
    }
  }

  void SendMasterHandoffResponse(uint8_t target_device, bool accepted) {
    std::vector<uint8_t> payload(kControlPayloadSize, 0x00);
    payload[0x00] = 0x01;
    payload[0x01] = 0x00;
    payload[kControlPayloadDeviceNumber] = config_.device_number;
    payload[0x03] = 0x00;
    payload[0x04] = 0x08;
    payload[kControlPayloadSender] = config_.device_number;
    payload[kControlPayloadCommand] = accepted ? 0x01 : 0x00;

    const auto packet =
        BuildPacket(PacketType::kMasterHandoffResponse, config_.device_name, payload);
    const auto target_ip = LookupDeviceIp(target_device);
    const auto addr = MakeSockaddr(target_ip.value_or(config_.broadcast_address),
                                   kBeatPort);
    const ssize_t result = beat_socket_.SendTo(packet, addr);
    RecordSendResult("master_handoff_response", result, packet.size());
  }

  // Respond to an incoming sync control packet.
  void HandleSyncControl(uint8_t sender_device, uint8_t command) {
    switch (command) {
      case static_cast<uint8_t>(SyncCommand::kEnableSync):
        SetSynced(true);
        break;
      case static_cast<uint8_t>(SyncCommand::kDisableSync):
        SetSynced(false);
        break;
      case static_cast<uint8_t>(SyncCommand::kBecomeMaster):
        RequestMasterRoleInternal();
        break;
      default:
        break;
    }
    (void)sender_device;
  }

  // Respond to a master handoff request when we are the master.
  void HandleMasterHandoffRequest(uint8_t requester) {
    bool should_respond = false;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (state_.master) {
        handoff_to_device_ = requester;
        should_respond = true;
      }
    }
    if (should_respond) {
      SendMasterHandoffResponse(requester, true);
    }
  }

  // Handle a master handoff response to our request.
  void HandleMasterHandoffResponse(uint8_t responder, bool accepted) {
    if (!accepted) {
      return;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (requesting_master_from_ != 0 && responder == requesting_master_from_) {
      // Response acknowledged; actual handoff begins when M_h targets us.
    }
  }

  // Request the tempo master role using the observed master device.
  void RequestMasterRoleInternal() {
    uint8_t master_device = 0;
    const auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (state_.master) {
        return;
      }
      if (!master_status_.has_value()) {
        state_.master = true;
        state_.synced = true;
        last_sent_beat_ = 0;
        requesting_master_from_ = 0;
        master_request_attempts_ = 0;
        master_request_time_ = std::chrono::steady_clock::time_point{};
        master_request_start_time_ = std::chrono::steady_clock::time_point{};
        return;
      }
      master_device = master_status_->device_number;
      if (master_device == config_.device_number) {
        state_.master = true;
        state_.synced = true;
        requesting_master_from_ = 0;
        master_request_attempts_ = 0;
        master_request_time_ = std::chrono::steady_clock::time_point{};
        master_request_start_time_ = std::chrono::steady_clock::time_point{};
        return;
      }
      if (requesting_master_from_ == master_device) {
        if (now - master_request_time_ < config_.master_request_retry_interval) {
          return;
        }
        requesting_master_from_ = 0;
      }
      requesting_master_from_ = master_device;
      master_request_time_ = now;
      master_request_start_time_ = now;
      master_request_attempts_ = 1;
    }
    SendMasterHandoffRequestInternal(master_device);
  }

  Config config_;
  std::atomic<bool> running_{false};
  UdpSocket beat_socket_;
  UdpSocket status_socket_;
  UdpSocket device_socket_;
  UdpSocket announce_socket_;

  BeatCallback beat_cb_;
  StatusCallback status_cb_;
  DeviceCallback device_cb_;
  DeviceEventCallback device_event_cb_;
  std::string start_error_;
  SessionMetricsAtomic metrics_;

  mutable std::mutex callback_mutex_;
  mutable std::mutex state_mutex_;
  std::condition_variable state_cv_;
  State state_;
  BeatClock clock_;
  uint32_t packet_counter_ = 0;
  uint32_t last_sent_beat_ = 0;
  uint8_t handoff_to_device_ = 0xff;
  uint8_t requesting_master_from_ = 0;
  std::chrono::steady_clock::time_point master_request_time_{};
  std::chrono::steady_clock::time_point master_request_start_time_{};
  int master_request_attempts_ = 0;

  std::optional<StatusInfo> master_status_;
  uint8_t master_device_number_ = 0;
  uint32_t master_beat_number_ = 0;

  mutable std::mutex devices_mutex_;
  std::unordered_map<uint8_t, DeviceRecord> devices_;

  mutable std::mutex capture_mutex_;
  std::ofstream capture_stream_;
  std::ifstream replay_stream_;
  bool replay_mode_ = false;

  std::thread recv_thread_;
  std::thread beat_thread_;
  std::thread status_thread_;
  std::thread announce_thread_;
  std::thread prune_thread_;
};

Session::Session(Config config) : impl_(new Impl(std::move(config))) {}

Session::~Session() { impl_->Stop(); }

bool Session::Start() { return impl_->Start(); }
void Session::Stop() { impl_->Stop(); }

void Session::SetBeatCallback(BeatCallback cb) { impl_->SetBeatCallback(std::move(cb)); }
void Session::SetStatusCallback(StatusCallback cb) { impl_->SetStatusCallback(std::move(cb)); }
void Session::SetDeviceCallback(DeviceCallback cb) { impl_->SetDeviceCallback(std::move(cb)); }
void Session::SetDeviceEventCallback(DeviceEventCallback cb) {
  impl_->SetDeviceEventCallback(std::move(cb));
}

void Session::SetTempo(double bpm) { impl_->SetTempo(bpm); }
void Session::SetPitchPercent(double percent) { impl_->SetPitchPercent(percent); }
void Session::SetPlaying(bool playing) { impl_->SetPlaying(playing); }
void Session::SetMaster(bool master) { impl_->SetMaster(master); }
void Session::SetSynced(bool synced) { impl_->SetSynced(synced); }
void Session::SetBeat(uint32_t beat, uint8_t beat_within_bar) {
  impl_->SetBeat(beat, beat_within_bar);
}

void Session::SendBeat() { impl_->SendBeat(); }
void Session::SendStatus() { impl_->SendStatus(); }
void Session::SendSyncControl(uint8_t target_device, SyncCommand command) {
  impl_->SendSyncControl(target_device, command);
}
void Session::RequestMasterRole() { impl_->RequestMasterRole(); }
void Session::SendMasterHandoffRequest(uint8_t target_device) {
  impl_->SendMasterHandoffRequest(target_device);
}

std::optional<StatusInfo> Session::GetTempoMaster() const {
  return impl_->GetTempoMaster();
}

std::vector<DeviceInfo> Session::GetDevices() const {
  return impl_->GetDevices();
}

std::string Session::GetLastError() const {
  return impl_->GetLastError();
}

SessionMetrics Session::GetMetrics() const {
  return impl_->GetMetrics();
}

#ifdef PROLINK_TESTING
namespace test {

struct BeatClockTester::Impl {
  explicit Impl(int beats_per_bar) : clock(beats_per_bar) {}
  BeatClock clock;
};

BeatClockTester::BeatClockTester(int beats_per_bar)
    : impl_(new BeatClockTester::Impl(beats_per_bar)) {}

BeatClockTester::~BeatClockTester() = default;

void BeatClockTester::SetTempo(double bpm) { impl_->clock.SetTempo(bpm); }

void BeatClockTester::SetPlaying(bool playing) { impl_->clock.SetPlaying(playing); }

void BeatClockTester::AlignToBeatNumber(uint32_t beat, uint8_t beat_within_bar,
                                        std::chrono::steady_clock::time_point when) {
  impl_->clock.AlignToBeatNumber(beat, beat_within_bar, when);
}

void BeatClockTester::AlignToBeatWithinBar(uint8_t beat_within_bar,
                                           std::chrono::steady_clock::time_point when) {
  impl_->clock.AlignToBeatWithinBar(beat_within_bar, when);
}

BeatClockSnapshot BeatClockTester::Snapshot(std::chrono::steady_clock::time_point now) const {
  const BeatSnapshot snapshot = impl_->clock.Snapshot(now);
  BeatClockSnapshot out;
  out.beat = snapshot.beat;
  out.beat_within_bar = snapshot.beat_within_bar;
  out.tempo_bpm = snapshot.tempo_bpm;
  out.beat_interval_ms = snapshot.beat_interval_ms;
  out.bar_interval_ms = snapshot.bar_interval_ms;
  return out;
}

std::vector<uint8_t> BuildSyncControlPacket(uint8_t device_number,
                                            const std::string& device_name,
                                            SyncCommand command) {
  std::vector<uint8_t> payload(kControlPayloadSize, 0x00);
  payload[0x00] = 0x01;
  payload[0x01] = 0x00;
  payload[kControlPayloadDeviceNumber] = device_number;
  payload[0x03] = 0x00;
  payload[0x04] = 0x08;
  payload[kControlPayloadSender] = device_number;
  payload[kControlPayloadCommand] = static_cast<uint8_t>(command);
  return BuildPacket(PacketType::kSyncControl, device_name, payload);
}

std::vector<uint8_t> BuildMasterHandoffRequestPacket(uint8_t device_number,
                                                     const std::string& device_name) {
  std::vector<uint8_t> payload(kHandoffRequestPayloadSize, 0x00);
  payload[0x00] = 0x01;
  payload[0x01] = 0x00;
  payload[kControlPayloadDeviceNumber] = device_number;
  payload[0x03] = 0x00;
  payload[0x04] = 0x04;
  payload[kControlPayloadSender] = device_number;
  return BuildPacket(PacketType::kMasterHandoffRequest, device_name, payload);
}

std::vector<uint8_t> BuildMasterHandoffResponsePacket(uint8_t device_number,
                                                      const std::string& device_name,
                                                      bool accepted) {
  std::vector<uint8_t> payload(kControlPayloadSize, 0x00);
  payload[0x00] = 0x01;
  payload[0x01] = 0x00;
  payload[kControlPayloadDeviceNumber] = device_number;
  payload[0x03] = 0x00;
  payload[0x04] = 0x08;
  payload[kControlPayloadSender] = device_number;
  payload[kControlPayloadCommand] = accepted ? 0x01 : 0x00;
  return BuildPacket(PacketType::kMasterHandoffResponse, device_name, payload);
}

std::vector<uint8_t> BuildBeatPacket(uint8_t device_number,
                                     const std::string& device_name,
                                     uint32_t bpm,
                                     uint32_t pitch,
                                     uint8_t beat_within_bar,
                                     uint32_t next_beat_ms,
                                     uint32_t next_bar_ms) {
  std::vector<uint8_t> payload = BeatPayloadTemplate();
  assert(payload.size() >= kOffsetBeatPayloadDeviceNumber2 + 1);
  payload[kOffsetBeatPayloadDeviceNumber] = device_number;
  payload[kOffsetBeatPayloadDeviceNumber2] = device_number;
  WriteBe32(payload, kOffsetBeatPayloadInterval, next_beat_ms);
  WriteBe32(payload, kOffsetBeatPayloadNextBar, next_bar_ms);
  WriteBe24(payload, kOffsetBeatPayloadPitch, pitch);
  WriteBe16(payload, kOffsetBeatPayloadBpm, bpm);
  payload[kOffsetBeatPayloadBeatWithinBar] = beat_within_bar;
  return BuildPacket(PacketType::kBeat, device_name, payload);
}

std::vector<uint8_t> BuildStatusPacket(uint8_t device_number,
                                       const std::string& device_name,
                                       uint32_t bpm,
                                       uint32_t pitch,
                                       uint32_t beat_number,
                                       uint8_t beat_within_bar,
                                       bool is_master,
                                       bool is_synced,
                                       bool is_playing,
                                       uint8_t master_handoff_to) {
  std::vector<uint8_t> payload = StatusPayloadTemplate();
  assert(payload.size() >= kOffsetStatusPayloadPacketCounter + 4);
  payload[kOffsetStatusPayloadDeviceNumber] = device_number;
  uint8_t flags = 0;
  if (is_master) {
    flags |= kStatusFlagMaster;
  }
  if (is_synced) {
    flags |= kStatusFlagSynced;
  }
  if (is_playing) {
    flags |= kStatusFlagPlaying;
  }
  payload[kOffsetStatusPayloadFlagByte] = flags;
  payload[kOffsetStatusPayloadMasterHandoff] = master_handoff_to;
  WriteBe24(payload, kOffsetStatusPayloadPitch, pitch);
  WriteBe16(payload, kOffsetStatusPayloadBpm, bpm);
  WriteBe32(payload, kOffsetStatusPayloadBeatNumber, beat_number);
  payload[kOffsetStatusPayloadBeatWithinBar] = beat_within_bar;
  return BuildPacket(PacketType::kCdjStatus, device_name, payload);
}

std::vector<uint8_t> BuildKeepAlivePacket(uint8_t device_number,
                                          uint8_t device_type,
                                          const std::string& device_name,
                                          const std::array<uint8_t, 6>& mac_address,
                                          const std::string& ip_address) {
  std::array<uint8_t, 4> ip_bytes{};
  if (!ip_address.empty()) {
    in_addr addr{};
    if (inet_pton(AF_INET, ip_address.c_str(), &addr) == 1) {
      std::memcpy(ip_bytes.data(), &addr, ip_bytes.size());
    }
  }

  std::array<uint8_t, kDeviceNameLength> name_bytes{};
  const size_t copy_len = std::min(device_name.size(), name_bytes.size());
  std::memcpy(name_bytes.data(), device_name.data(), copy_len);

  std::vector<uint8_t> packet;
  packet.reserve(kKeepAlivePacketSize);
  packet.insert(packet.end(), kProlinkHeader, kProlinkHeader + kHeaderSize);
  packet.push_back(static_cast<uint8_t>(PacketType::kDeviceKeepAlive));
  packet.push_back(0x00);
  packet.insert(packet.end(), name_bytes.begin(), name_bytes.end());
  packet.push_back(0x01);
  packet.push_back(0x02);
  packet.push_back(0x00);
  packet.push_back(static_cast<uint8_t>(kKeepAlivePacketSize));
  packet.push_back(device_number);
  packet.push_back(device_type);
  packet.insert(packet.end(), mac_address.begin(), mac_address.end());
  packet.insert(packet.end(), ip_bytes.begin(), ip_bytes.end());
  packet.push_back(0x01);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(device_type);
  packet.push_back(0x00);
  return packet;
}

bool ParseBeatPacket(const std::vector<uint8_t>& data, BeatInfo* out) {
  return ParseBeat(data.data(), data.size(), out);
}

bool ParseStatusPacket(const std::vector<uint8_t>& data, StatusInfo* out) {
  return ParseStatus(data.data(), data.size(), out);
}

bool ParseKeepAlivePacket(const std::vector<uint8_t>& data, DeviceInfo* out) {
  if (!out) {
    return false;
  }
  KeepAliveInfo info;
  if (!ParseKeepAlive(data.data(), data.size(), &info)) {
    return false;
  }
  out->device_number = info.device_number;
  out->device_type = info.device_type;
  out->device_name = info.device_name;
  out->ip_address = info.ip_address;
  out->mac_address = info.mac_address;
  out->last_seen = std::chrono::steady_clock::now();
  return true;
}

void InjectKeepAlive(Session& session,
                     uint8_t device_number,
                     uint8_t device_type,
                     const std::string& device_name,
                     const std::string& ip_address,
                     const std::array<uint8_t, 6>& mac_address) {
  KeepAliveInfo info;
  info.device_number = device_number;
  info.device_type = device_type;
  info.device_name = device_name;
  info.ip_address = ip_address;
  info.mac_address = mac_address;
  session.impl_->UpdateDeviceFromKeepAlive(info);
}

void SetDeviceLastSeen(Session& session,
                       uint8_t device_number,
                       std::chrono::steady_clock::time_point when) {
  std::lock_guard<std::mutex> lock(session.impl_->devices_mutex_);
  auto it = session.impl_->devices_.find(device_number);
  if (it != session.impl_->devices_.end()) {
    it->second.info.last_seen = when;
  }
}

void PruneDevices(Session& session,
                  std::chrono::steady_clock::time_point now) {
  session.impl_->RunPrune(now);
}

size_t GetDeviceRecordCount(Session& session) {
  std::lock_guard<std::mutex> lock(session.impl_->devices_mutex_);
  return session.impl_->devices_.size();
}

}  // namespace test
#endif

}  // namespace prolink
