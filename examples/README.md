# Examples

This directory contains example applications demonstrating how to use the prolink-cpp library.

---

## 🎧 listener.cpp

**A passive monitoring example** that listens for beat and status packets on the network.

### What it does:
- Receives and displays beat packets from CDJs
- Monitors status changes (playing, master, synced)
- Prints device names and BPM information

### Usage:
```bash
./prolink_listener
```

Press Enter to stop.

### Use case:
Perfect for:
- Monitoring DJ sets
- Logging beat information
- Testing network connectivity
- Understanding the protocol

---

## 🎛️ virtual_cdj.cpp

**A simple virtual CDJ** that announces itself on the network and sends beat/status packets.

### What it does:
- Creates a virtual CDJ device
- Sends keep-alive announcements
- Broadcasts beat packets at a fixed BPM
- Sends status packets

### Usage:
```bash
./prolink_virtual_cdj <device_ip> <broadcast_ip> <mac> [device_id] [name] [tempo]
```

**Example:**
```bash
./prolink_virtual_cdj 192.168.1.100 192.168.1.255 aa:bb:cc:dd:ee:ff 7 MyVirtualCDJ 128

# With follow-master mode:
./prolink_virtual_cdj 192.168.1.100 192.168.1.255 aa:bb:cc:dd:ee:ff 7 Follower 120 --follow-master
```

### Use case:
- Testing CDJ integration without hardware
- Creating a tempo source for other devices
- Automated DJ booth testing

---

## 🎮 virtual_cdj_interactive.cpp

**An advanced interactive virtual CDJ** with a menu-driven interface for real-time control.

### What it does:
- Everything virtual_cdj.cpp does, PLUS:
- Interactive menu for changing all parameters
- Real-time BPM/pitch adjustment
- Play/stop control
- Master/slave toggling
- Sync control
- Beat position manipulation
- Device discovery display
- Send sync commands to other devices
- Master handoff requests

### Usage:
```bash
./prolink_virtual_cdj_interactive <device_ip> <broadcast_ip> <mac> [device_id] [name] [tempo]
```

**Example:**
```bash
./prolink_virtual_cdj_interactive 10.0.0. 192.168.1.255 aa:bb:cc:dd:ee:ff 7 ControlCenter 128
```

### Menu Options:

**Playback Control:**
- `1` - Set BPM/Tempo (20-300)
- `2` - Set Pitch (-100% to +100%)
- `3` - Toggle Playing/Stopped
- `4` - Set Beat Position
- `5` - Set Beat Within Bar (1-4)

**Master/Sync Control:**
- `6` - Toggle Master/Slave
- `7` - Toggle Sync On/Off
- `8` - Request Master Role (handoff)
- `9` - Send Sync Command to Device

**Information:**
- `s` - Show Current State
- `d` - Show Discovered Devices
- `r` - Refresh Screen
- `h` - Show Help
- `q` - Quit

### Features:

#### Color-Coded Output
- Green: Success messages, new devices
- Yellow: Warnings, state changes
- Red: Errors
- Cyan: Headers and prompts
- Magenta: Master status

#### Real-Time Monitoring
- Displays current tempo master
- Shows discovered devices with IP addresses
- Notifies when devices join/leave network
- Shows device IDs and names

#### Full Control
Change any parameter on the fly:
- Adjust tempo while playing
- Change beat position mid-track
- Toggle master/slave without restarting
- Send sync commands to other players

### Use case:
Perfect for:
- **Interactive testing** - Test protocol behavior with live changes
- **DJ booth simulation** - Simulate multiple players with different roles
- **Development** - Develop and test DJ software integrations
- **Education** - Learn the Pro DJ Link protocol interactively
- **Debugging** - Isolate network issues with controlled inputs
- **Lighting control testing** - Generate precise beat patterns

### Example Session:

```
Starting the interactive CDJ:
$ ./prolink_virtual_cdj_interactive 10.0.0.82 10.0.0.255 b6:5f:9c:63:d6:7d 2 TestDeck 128

1. Set BPM to 140
2. Press '3' to start playing
3. Wait for other CDJs to appear in device list
4. Press '9' to send sync command to device 1
5. Press '8' to request master role
6. Press '2' to adjust pitch to +5%
7. Press 'q' to quit
```

### Tips:

**Network Setup:**
- Make sure your computer is on the same network as the CDJs
- Use your subnet's broadcast address (e.g., 192.168.1.255)
- NOT the generic broadcast 255.255.255.255

**Device IDs:**
- Use 1-4 if you want to act like a real player
- Use 5-15 for virtual/software devices
- Use 16+ for custom applications

**Testing Master Handoff:**
1. Start with master=false (slave)
2. Wait for a real CDJ to become master
3. Press '8' to request master role
4. Watch the M_h field in status packets
5. You should receive master when CDJ accepts

**Sync Commands:**
- Enable/Disable Sync: Controls another device's sync state
- Become Master: Tells another device to request master role

---

## control_demo.cpp

**Device discovery and sync control demonstration.**

### What it does:
- Discovers all devices on the network
- Displays device information
- Demonstrates sending sync control packets
- Shows master handoff request flow

### Usage:
```bash
./prolink_control_demo
```

Waits 5 seconds for discovery, then:
1. Lists all discovered devices
2. Sends sync ON to first device
3. Waits 500ms
4. Sends sync OFF to first device
5. Requests master role

### Use case:
- Understanding device discovery
- Testing sync control commands
- Learning the control flow

---

## 🚀 Building the Examples

All examples are built automatically when you build the project:

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Executables will be in the `build/` directory:
- `prolink_listener`
- `prolink_virtual_cdj`
- `prolink_virtual_cdj_interactive`
- `prolink_control_demo`

---

## Creating Your Own Example

Use these examples as templates:

**For passive monitoring:**
```cpp
#include "prolink/prolink.h"

int main() {
  prolink::Config config;
  config.send_beats = false;      // Receive only
  config.send_status = false;
  config.send_announces = false;

  prolink::Session session(config);
  session.SetBeatCallback([](const prolink::BeatInfo& beat) {
    // Your code here
  });

  session.Start();
  // ... wait or process ...
  session.Stop();
  return 0;
}
```

**For active participation:**
```cpp
#include "prolink/prolink.h"

int main() {
  prolink::Config config;
  config.device_ip = "192.168.1.100";
  config.broadcast_address = "192.168.1.255";
  config.mac_address = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

  config.send_beats = true;       // Send packets
  config.send_status = true;
  config.send_announces = true;

  prolink::Session session(config);
  session.Start();

  // Control the session
  session.SetTempo(128.0);
  session.SetPlaying(true);

  // ... your application ...

  session.Stop();
  return 0;
}
```

---

## Troubleshooting

**No devices discovered:**
- Check network connectivity (`ping` the CDJ IP)
- Verify broadcast address is correct for your subnet
- Make sure firewall allows UDP ports 50000-50002
- Wait at least 5 seconds for discovery

**Virtual CDJ not visible:**
- Ensure `device_ip` is set correctly
- Ensure `mac_address` is set (even if fake)
- Verify `send_announces = true`
- Check broadcast address (use subnet-specific)
- CDJs may take 5-10 seconds to recognize new devices

**Compilation errors:**
- Make sure you're using C++17 or later
- Check that prolink-cpp library is built first
- Verify CMake version >= 3.16

**Runtime crashes:**
- Check socket permissions (may need elevated privileges on some systems)
- Ensure no other application is bound to ports 50000-50002
- Verify IP addresses are valid

---

## Further Reading

- [Main README](../README.md) - Full library documentation
- [API Reference](../include/prolink/prolink.h) - Header file with all public APIs
- [dysentery](https://github.com/Deep-Symmetry/dysentery) - Protocol documentation

---

**Need help?** Open an issue on GitHub with:
- Which example you're using
- Your network setup (IP addresses, subnet)
- What you expected vs. what happened
- Any error messages
