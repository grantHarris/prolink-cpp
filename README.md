# prolink-cpp

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/yourusername/prolink-cpp)

A modern (C++17 my application limits me to 17 or older features) library for interacting with Pioneer DJ equipment using the **Pro DJ Link** protocol. Monitor beats, sync to tempo masters, and control CDJs.

---

## Features

### Core Functionality
- **Receive beat packets** - Get beat/bar timing from CDJs and mixers
- **Receive status packets** - Monitor playback state, BPM, pitch, sync status
- **Virtual CDJ mode** - Act as a virtual player on the network
- **Device discovery** - Automatically detect all Pro DJ Link devices
- **Tempo master tracking** - Identify and follow the current tempo master
- **Master handoff** - Request and negotiate tempo master role
- **Sync control** - Send sync enable/disable commands to players

### Supported Operations
- Beat-synchronized callbacks
- Follow-master mode for automatic tempo alignment
- Custom virtual CDJ with configurable device ID, name, and MAC
- Multi-device tracking with lifecycle events (seen/updated/expired)
- Optional packet capture/replay for offline debugging
- Session metrics counters for monitoring

---

## Quick Start

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Run Examples

**Listen for beats and status:**
```bash
./prolink_listener
```

**Create a virtual CDJ:**
```bash
./prolink_virtual_cdj 192.168.1.100 192.168.1.255 aa:bb:cc:dd:ee:ff 7 MyController 128
```

**Interactive virtual CDJ with full control:**
```bash
./prolink_virtual_cdj_interactive 192.168.1.100 192.168.1.255 aa:bb:cc:dd:ee:ff 7 MyController 128
```

**Discover devices and test sync control:**
```bash
./prolink_control_demo
```

---

## Usage

### Listen for Beats

```cpp
#include "prolink/prolink.h"
#include <iostream>

int main() {
  prolink::Config config;
  config.send_beats = false;
  config.send_status = false;
  config.send_announces = false;

  prolink::Session session(config);

  // Get notified on every beat
  session.SetBeatCallback([](const prolink::BeatInfo& beat) {
    std::cout << "Beat from " << beat.device_name
              << " @ " << (beat.bpm / 100.0) << " BPM"
              << " [" << +beat.beat_within_bar << "/4]"
              << std::endl;
  });

  // Monitor playback status changes
  session.SetStatusCallback([](const prolink::StatusInfo& status) {
    if (status.is_master) {
      std::cout << status.device_name << " is tempo master" << std::endl;
    }
  });

  session.Start();
  std::cin.get();  // Wait for Enter
  session.Stop();
  return 0;
}
```

### Create a Virtual CDJ

```cpp
#include "prolink/prolink.h"

int main() {
  prolink::Config config;
  config.device_name = "MyVisualizer";
  config.device_number = 0x07;  // Device ID
  config.device_ip = "192.168.1.100";
  config.broadcast_address = "192.168.1.255";
  config.mac_address = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

  config.tempo_bpm = 128.0;
  config.playing = true;
  config.master = true;

  config.send_beats = true;
  config.send_status = true;
  config.send_announces = true;

  prolink::Session session(config);
  session.Start();

  // Other CDJs will now see "MyVisualizer" on the network
  // and receive beat packets at 128 BPM

  std::cin.get();
  session.Stop();
  return 0;
}
```

### Follow the Tempo Master

```cpp
prolink::Config config;
config.device_ip = "192.168.1.100";
config.broadcast_address = "192.168.1.255";
config.mac_address = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
config.follow_master = true;  // Automatically sync to current master
config.synced = true;

prolink::Session session(config);
session.Start();

// Your session will now automatically align its internal beat clock
// to match the current tempo master's BPM and beat phase
```

---

## API Overview

### Session Management
```cpp
prolink::Session session(config);
bool success = session.Start();  // Returns false on error
session.Stop();
```

### Callbacks
```cpp
session.SetBeatCallback([](const prolink::BeatInfo& beat) { /* ... */ });
session.SetStatusCallback([](const prolink::StatusInfo& status) { /* ... */ });
session.SetDeviceCallback([](const prolink::DeviceInfo& device) { /* ... */ });
session.SetDeviceEventCallback([](const prolink::DeviceEvent& event) { /* ... */ });
```

### Control
```cpp
session.SetTempo(128.5);                    // Set local tempo
session.SetPlaying(true);                   // Start/stop playback
session.SetMaster(true);                    // Become tempo master
session.SendSyncControl(target_device, prolink::SyncCommand::kEnableSync);
session.RequestMasterRole();                // Request master handoff
```

### Query
```cpp
auto devices = session.GetDevices();        // All discovered devices
auto master = session.GetTempoMaster();     // Current tempo master (optional)
std::string error = session.GetLastError(); // Last Start() error message
auto metrics = session.GetMetrics();        // Packet/error counters
```

---

## Configuration

```cpp
prolink::Config config;

// Identity
config.device_name = "prolink-cpp";         // Device name (max 20 chars)
config.device_number = 0x07;                // Device ID (1-4 for players, 0x07+ for virtual)
config.device_type = 0x01;                  // 0x01=CDJ, 0x03=Mixer, 0x04=Rekordbox

// Network
config.bind_address = "0.0.0.0";            // Local bind address
config.broadcast_address = "192.168.1.255"; // Subnet broadcast (NOT 255.255.255.255!)
config.device_ip = "192.168.1.100";         // Our IP (required for announces)
config.mac_address = {0xaa, ...};           // MAC address (required for announces)

// Behavior
config.tempo_bpm = 120.0;                   // Initial tempo
config.playing = false;                     // Playback state
config.master = false;                      // Tempo master state
config.synced = false;                      // Sync state
config.follow_master = false;               // Auto-align to tempo master

// Transmission
config.send_beats = true;                   // Send beat packets
config.send_status = true;                  // Send status packets
config.send_announces = true;               // Send keep-alive packets

// Diagnostics
config.log_callback = [](const std::string& message) {
  // Custom logger (optional)
};
config.capture_file = "";                   // Capture incoming packets
config.replay_file = "";                    // Replay packets from capture

// Timing
config.status_interval_ms = 200;            // Status packet interval
config.announce_interval_ms = 1500;         // Keep-alive interval
config.beats_per_bar = 4;                   // Time signature
```

**Important:** Use your subnet's broadcast address (e.g., `192.168.1.255`), not `255.255.255.255`, for reliable operation.

---

## Testing

### Build with Tests
```bash
cmake -DPROLINK_BUILD_TESTS=ON -DPROLINK_FETCH_GTEST=ON ..
cmake --build .
```

### Run Tests
```bash
ctest --output-on-failure
# or
./prolink_tests
```

---

## Implementation Status

This port is a work in progress and does not implement all aspects of the prolink protocol yet.

### Implemented
- Core beat/status packet parsing (CDJ-2000, XDJ, CDJ-3000)
- UDP send/receive on ports 50000-50002
- Virtual CDJ announcement and participation
- Beat clock with tempo/pitch tracking
- Follow-master mode (automatic tempo sync)
- Device discovery and lifecycle management
- Sync control packets (enable/disable sync)
- Master handoff protocol (request/response)
- Master role negotiation (M_h field handling)
- Thread-safe API with exception-safe callbacks
- Config validation with error reporting

---

## Known Issues & Limitations

### Platform Support
- **Currently**: Linux and macOS only (POSIX sockets)

### Protocol Coverage
- **Not implemented**: Database queries, waveforms, artwork, NFS access

---

### External Resources
- [dysentery Protocol Analysis](https://djl-analysis.deepsymmetry.org/)
- [Pro DJ Link Research](https://github.com/Deep-Symmetry/dysentery/blob/master/doc/Analysis.pdf) - Protocol PDF

---

## Contributing

Contributions are welcome! This project is in active development.

### How to Contribute
1. **Report bugs** - Open an issue with reproduction steps
2. **Suggest features** - Describe your use case
3. **Submit PRs** - Add tests, follow existing style
4. **Capture packets** - Real hardware data for testing is valuable!

### Development Setup
```bash
# Clone repository
git clone https://github.com/grantHarris/prolink-cpp.git
cd prolink-cpp

# Build with tests
mkdir build && cd build
cmake -DPROLINK_BUILD_TESTS=ON -DPROLINK_FETCH_GTEST=ON ..
cmake --build .

# Run tests
ctest --output-on-failure
```

### Production Use
This library has been tested with:
- CDJ-3000


## Community & Support

- **Issues:** [GitHub Issues](https://github.com/yourusername/prolink-cpp/issues)
- **Discussions:** [GitHub Discussions](https://github.com/yourusername/prolink-cpp/discussions)

---


## Credits & Acknowledgments

This library would not be possible without the reverse-engineering work done by:

### [dysentery](https://github.com/Deep-Symmetry/dysentery) by Deep Symmetry

James Elliott ([@brunchboy](https://github.com/brunchboy)) and the Deep Symmetry team deserve credit for their meticulous documentation.
- [dysentery Documentation](https://djl-analysis.deepsymmetry.org/)
- [GitHub Repository](https://github.com/Deep-Symmetry/dysentery)

### [beat-link](https://github.com/Deep-Symmetry/beat-link) by Deep Symmetry

- [GitHub Repository](https://github.com/Deep-Symmetry/beat-link)
- [Documentation](https://deepsymmetry.org/beatlink/)

### [prolink-connect](https://github.com/EvanPurkhiser/prolink-connect) by Evan Purkhiser

- [GitHub Repository](https://github.com/EvanPurkhiser/prolink-connect)
- [NPM Package](https://www.npmjs.com/package/prolink-connect)

### Related Projects
- [**beat-link-trigger**](https://github.com/Deep-Symmetry/beat-link-trigger) - Application for triggering events based on CDJ activity
- [**crate-digger**](https://github.com/Deep-Symmetry/crate-digger) - Rekordbox database analysis
- [**open-beat-control**](https://github.com/RubenInglada/open-beat-control) - Python library for Pro DJ Link

---

## License

MIT License - see [LICENSE](LICENSE) file for details.

### Third-Party Licenses
- **GoogleTest** - BSD 3-Clause License (testing only)
