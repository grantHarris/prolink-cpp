#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace prolink {

class Session;

#ifdef PROLINK_TESTING
namespace test {
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

/**
 * Well-known Pro DJ Link UDP ports.
 */
constexpr uint16_t kAnnouncePort = 50000;
constexpr uint16_t kBeatPort = 50001;
constexpr uint16_t kStatusPort = 50002;

/**
 * Protocol constants used by beat/status parsing.
 */
constexpr uint8_t kDeviceNameLength = 20;
constexpr uint32_t kNeutralPitch = 0x100000;

/**
 * Packet type identifiers used in the first byte after the magic header.
 */
enum class PacketType : uint8_t {
  kDeviceKeepAlive = 0x06,
  kCdjStatus = 0x0a,
  kMasterHandoffRequest = 0x26,
  kMasterHandoffResponse = 0x27,
  kBeat = 0x28,
  kSyncControl = 0x2a,
};

/**
 * Sync control commands (packet type 0x2a).
 */
enum class SyncCommand : uint8_t {
  kEnableSync = 0x10,
  kDisableSync = 0x20,
  kBecomeMaster = 0x01,
};

/**
 * Basic device discovery information from keep-alive packets.
 */
struct DeviceInfo {
  /// Player/device number reported in keep-alive.
  uint8_t device_number = 0;
  /// Device type byte reported in keep-alive (raw value).
  uint8_t device_type = 0;
  /// Device name field (trimmed ASCII).
  std::string device_name;
  /// IP address reported by the device.
  std::string ip_address;
  /// MAC address reported by the device.
  std::array<uint8_t, 6> mac_address = {0, 0, 0, 0, 0, 0};
  /// Last time a packet was observed from this device.
  std::chrono::steady_clock::time_point last_seen;
};

/**
 * Device lifecycle events emitted by discovery tracking.
 */
enum class DeviceEventType {
  kSeen,
  kUpdated,
  kExpired,
};

struct DeviceEvent {
  DeviceEventType type = DeviceEventType::kSeen;
  DeviceInfo device;
};

/**
 * Lightweight counters for packet flow and error reporting.
 */
struct SessionMetrics {
  uint64_t packets_received = 0;
  uint64_t packets_sent = 0;
  uint64_t parse_errors = 0;
  uint64_t send_errors = 0;
  uint64_t callback_exceptions = 0;
};

/**
 * Beat packet data parsed from broadcast traffic on port 50001.
 */
struct BeatInfo {
  /// Player/device number (1-4 on typical hardware, 0x07 for virtual CDJ).
  uint8_t device_number = 0;
  /// Device name field from the packet (trimmed ASCII).
  std::string device_name;
  /// Track BPM * 100 (e.g., 12050 == 120.50 BPM).
  uint32_t bpm = 0;
  /// Raw pitch value (0x000000 to 0x200000, neutral at 0x100000).
  uint32_t pitch = kNeutralPitch;
  /// Beat within the bar (1-4) as reported by the device.
  uint8_t beat_within_bar = 0;
  /// Time to next beat in ms at normal speed.
  uint32_t next_beat_ms = 0;
  /// Time to next bar in ms at normal speed.
  uint32_t next_bar_ms = 0;

  /// Compute effective BPM applying pitch to the track BPM.
  double effective_bpm() const;
};

/**
 * CDJ status packet data parsed from unicast/broadcast traffic on port 50002.
 */
struct StatusInfo {
  /// Player/device number reported by the device.
  uint8_t device_number = 0;
  /// Device name field from the packet (trimmed ASCII).
  std::string device_name;
  /// Track BPM * 100, if a track is loaded.
  std::optional<uint32_t> bpm;   // BPM * 100
  /// Raw pitch value (0x000000 to 0x200000, neutral at 0x100000).
  uint32_t pitch = kNeutralPitch;
  /// Absolute beat number within the track, if known.
  std::optional<uint32_t> beat;  // absolute beat number
  /// Beat within the bar (1-4) as reported by the device.
  uint8_t beat_within_bar = 0;
  /// Device number being handed the master role, or 0xff if none.
  uint8_t master_handoff_to = 0xff;
  /// Whether this device reports itself as tempo master.
  bool is_master = false;
  /// Whether this device reports itself as synced.
  bool is_synced = false;
  /// Whether this device reports itself as playing.
  bool is_playing = false;

  /// Compute effective BPM applying pitch to the track BPM, if available.
  std::optional<double> effective_bpm() const;
};

/**
 * Session configuration for sockets, identity, and timing behavior.
 */
struct Config {
  using LogCallback = std::function<void(const std::string&)>;

  /// Device name used in announce/status/beat packets (ASCII, padded to 20 bytes).
  std::string device_name = "prolink-cpp";
  /// Device/player number to report (0x01-0x04 for real players, 0x07 default).
  uint8_t device_number = 0x07;
  /// Device type byte (0x01 CDJ, 0x03 Mixer, 0x04 Rekordbox).
  uint8_t device_type = 0x01;  // CDJ
  /// MAC address used in announce packets.
  std::array<uint8_t, 6> mac_address = {0, 0, 0, 0, 0, 0};
  /// IPv4 address of this host used in announce packets.
  std::string device_ip;

  /// Local bind address for sockets (usually 0.0.0.0).
  std::string bind_address = "0.0.0.0";
  /// Broadcast address used for beat/status packets.
  std::string broadcast_address = "255.255.255.255";
  /// Broadcast address used for announce packets.
  std::string announce_address = "255.255.255.255";

  /// Status interval in milliseconds (CDJs send ~200 ms).
  int status_interval_ms = 200;
  /// Announce interval in milliseconds (keep-alives ~1500 ms).
  int announce_interval_ms = 1500;
  /// Beats per bar for local beat clock.
  int beats_per_bar = 4;

  /// Base tempo for the local beat clock (BPM).
  double tempo_bpm = 120.0;
  /// Pitch adjustment in percent (-100..+100).
  double pitch_percent = 0.0;
  /// Whether local playback is currently active.
  bool playing = false;
  /// Whether to report as tempo master in status packets.
  bool master = false;
  /// Whether to report synced in status packets.
  bool synced = false;

  /// Enable sending beat packets.
  bool send_beats = true;
  /// Enable sending status packets.
  bool send_status = true;
  /// Enable sending announce/keep-alive packets.
  bool send_announces = true;
  /// If true, align local clock to the current tempo master.
  bool follow_master = false;

  /// Optional log callback (defaults to stderr).
  LogCallback log_callback;

  /// Optional packet capture file (binary).
  std::string capture_file;
  /// Optional packet replay file (binary).
  std::string replay_file;

  /// Retry interval for tempo master handoff requests.
  std::chrono::milliseconds master_request_retry_interval{1000};
  /// Overall timeout for tempo master handoff requests.
  std::chrono::milliseconds master_request_timeout{5000};
  /// Maximum number of handoff retries before giving up (includes first request).
  int master_request_max_retries = 3;

  /// Device timeout for discovery pruning.
  std::chrono::milliseconds device_timeout{4000};
  /// How often to check for device expiry.
  std::chrono::milliseconds device_prune_interval{1000};

  /**
   * Validate configuration values.
   *
   * @param error Optional output string describing the first validation error.
   * @return true if the configuration is valid.
   */
  bool Validate(std::string* error = nullptr) const;
};

/**
 * Pro DJ Link session for sending/receiving beat and status traffic.
 */
class Session {
 public:
  using BeatCallback = std::function<void(const BeatInfo&)>;
  using StatusCallback = std::function<void(const StatusInfo&)>;
  using DeviceCallback = std::function<void(const DeviceInfo&)>;
  using DeviceEventCallback = std::function<void(const DeviceEvent&)>;

  /// Construct a session with the provided configuration.
  explicit Session(Config config);
  /// Stop background threads and close sockets.
  ~Session();

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  /// Open sockets and start background threads.
  bool Start();
  /// Stop background threads and close sockets.
  void Stop();

  /// Set callback invoked for each parsed beat packet.
  void SetBeatCallback(BeatCallback cb);
  /// Set callback invoked for each parsed status packet.
  void SetStatusCallback(StatusCallback cb);
  /// Set callback invoked when a device keep-alive is observed/updated.
  void SetDeviceCallback(DeviceCallback cb);
  /// Set callback invoked on device lifecycle events (seen/updated/expired).
  void SetDeviceEventCallback(DeviceEventCallback cb);

  /// Update local tempo (BPM) for beat/status sending.
  void SetTempo(double bpm);
  /// Update local pitch percent for beat/status sending.
  void SetPitchPercent(double percent);
  /// Toggle local playback state.
  void SetPlaying(bool playing);
  /// Toggle local tempo master state.
  void SetMaster(bool master);
  /// Toggle local sync state.
  void SetSynced(bool synced);
  /// Force local beat position (1-based beat and beat-within-bar).
  void SetBeat(uint32_t beat, uint8_t beat_within_bar);

  /// Immediately send a beat packet based on current local state.
  void SendBeat();
  /// Immediately send a status packet based on current local state.
  void SendStatus();
  /// Send a sync control packet to a target device.
  void SendSyncControl(uint8_t target_device, SyncCommand command);
  /// Request to become tempo master, triggering a handoff if needed.
  void RequestMasterRole();
  /// Send a master handoff request packet to a target device.
  void SendMasterHandoffRequest(uint8_t target_device);

  /// Return the last known tempo master status, if any.
  std::optional<StatusInfo> GetTempoMaster() const;
  /// Return the list of devices discovered via keep-alive packets.
  std::vector<DeviceInfo> GetDevices() const;
  /// Return the last Start() error message, if any.
  std::string GetLastError() const;
  /// Return metrics for packets, errors, and callbacks.
  SessionMetrics GetMetrics() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

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
};

}  // namespace prolink
