// Interactive virtual CDJ with menu-driven controls for all parameters.
#include "prolink/prolink.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// ANSI color codes for terminal output
const char* kColorReset = "\033[0m";
const char* kColorBold = "\033[1m";
const char* kColorGreen = "\033[32m";
const char* kColorYellow = "\033[33m";
const char* kColorBlue = "\033[34m";
const char* kColorMagenta = "\033[35m";
const char* kColorCyan = "\033[36m";
const char* kColorRed = "\033[31m";

std::atomic<bool> g_running{true};

bool ParseMac(const std::string& text, std::array<uint8_t, 6>* out) {
  if (!out) {
    return false;
  }
  std::array<uint8_t, 6> mac{};
  std::stringstream ss(text);
  std::string part;
  int idx = 0;
  while (std::getline(ss, part, ':')) {
    if (idx >= 6) {
      return false;
    }
    mac[idx++] = static_cast<uint8_t>(std::strtoul(part.c_str(), nullptr, 16));
  }
  if (idx != 6) {
    return false;
  }
  *out = mac;
  return true;
}

void ClearScreen() {
  std::cout << "\033[2J\033[H";  // ANSI clear screen and move cursor to home
}

void PrintHeader() {
  std::cout << kColorBold << kColorCyan;
  std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
  std::cout << "║        Interactive Pro DJ Link Virtual CDJ Controller         ║\n";
  std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
  std::cout << kColorReset << "\n";
}

void PrintCurrentState(prolink::Session& session) {
  auto tempo_master = session.GetTempoMaster();

  std::cout << kColorBold << "Current Virtual CDJ State:\n" << kColorReset;
  std::cout << "─────────────────────────────────────\n";

  if (tempo_master.has_value()) {
    std::cout << kColorYellow << "Tempo Master: " << kColorReset
              << tempo_master->device_name << " ("
              << +tempo_master->device_number << ")";
    if (tempo_master->bpm.has_value()) {
      std::cout << " @ " << (tempo_master->bpm.value() / 100.0) << " BPM";
    }
    std::cout << "\n";
  } else {
    std::cout << kColorYellow << "Tempo Master: " << kColorReset << "None detected\n";
  }
  std::cout << "\n";
}

void PrintDevices(prolink::Session& session) {
  auto devices = session.GetDevices();

  std::cout << kColorBold << "Discovered Devices (" << devices.size() << "):\n" << kColorReset;
  std::cout << "─────────────────────────────────────\n";

  if (devices.empty()) {
    std::cout << kColorYellow << "No devices discovered yet...\n" << kColorReset;
  } else {
    for (const auto& device : devices) {
      std::cout << "  [" << kColorGreen << std::setw(2) << +device.device_number
                << kColorReset << "] " << device.device_name;
      if (!device.ip_address.empty()) {
        std::cout << " @ " << device.ip_address;
      }
      std::cout << "\n";
    }
  }
  std::cout << "\n";
}

void PrintMenu() {
  std::cout << kColorBold << "Main Menu:\n" << kColorReset;
  std::cout << "─────────────────────────────────────\n";
  std::cout << kColorCyan << "Playback Control:\n" << kColorReset;
  std::cout << "  1. Set BPM/Tempo\n";
  std::cout << "  2. Set Pitch (%)\n";
  std::cout << "  3. Toggle Playing/Stopped\n";
  std::cout << "  4. Set Beat Position\n";
  std::cout << "  5. Set Beat Within Bar (1-4)\n";
  std::cout << "\n";

  std::cout << kColorMagenta << "Master/Sync Control:\n" << kColorReset;
  std::cout << "  6. Toggle Master/Slave\n";
  std::cout << "  7. Toggle Sync On/Off\n";
  std::cout << "  8. Request Master Role\n";
  std::cout << "  9. Send Sync Command to Device\n";
  std::cout << "\n";

  std::cout << kColorYellow << "Information:\n" << kColorReset;
  std::cout << "  s. Show Current State\n";
  std::cout << "  d. Show Discovered Devices\n";
  std::cout << "  r. Refresh Screen\n";
  std::cout << "\n";

  std::cout << kColorRed << "Other:\n" << kColorReset;
  std::cout << "  h. Show This Help\n";
  std::cout << "  q. Quit\n";
  std::cout << "\n";
  std::cout << kColorBold << "Enter choice: " << kColorReset;
}

void HandleSetBpm(prolink::Session& session) {
  std::cout << "\nCurrent BPM range: 20.00 - 300.00\n";
  std::cout << "Enter new BPM: ";
  double bpm;
  std::cin >> bpm;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  if (bpm < 20.0 || bpm > 300.0) {
    std::cout << kColorRed << "Error: BPM must be between 20 and 300\n" << kColorReset;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return;
  }

  session.SetTempo(bpm);
  std::cout << kColorGreen << "✓ BPM set to " << bpm << "\n" << kColorReset;
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

void HandleSetPitch(prolink::Session& session) {
  std::cout << "\nPitch range: -100.0% (half speed) to +100.0% (double speed)\n";
  std::cout << "Enter pitch percent: ";
  double pitch;
  std::cin >> pitch;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  if (pitch < -100.0 || pitch > 100.0) {
    std::cout << kColorRed << "Error: Pitch must be between -100 and +100\n" << kColorReset;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return;
  }

  session.SetPitchPercent(pitch);
  std::cout << kColorGreen << "✓ Pitch set to " << std::showpos << pitch
            << std::noshowpos << "%\n" << kColorReset;
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

bool g_playing = false;

void HandleTogglePlaying(prolink::Session& session) {
  g_playing = !g_playing;
  session.SetPlaying(g_playing);

  if (g_playing) {
    std::cout << kColorGreen << "▶ Playing\n" << kColorReset;
  } else {
    std::cout << kColorYellow << "⏸ Stopped\n" << kColorReset;
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

void HandleSetBeat(prolink::Session& session) {
  std::cout << "\nEnter beat number (1-999999): ";
  uint32_t beat;
  std::cin >> beat;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  if (beat == 0) {
    std::cout << kColorRed << "Error: Beat must be >= 1\n" << kColorReset;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return;
  }

  std::cout << "Enter beat within bar (1-4): ";
  uint8_t beat_within_bar;
  int temp;
  std::cin >> temp;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  beat_within_bar = static_cast<uint8_t>(temp);

  if (beat_within_bar < 1 || beat_within_bar > 4) {
    std::cout << kColorRed << "Error: Beat within bar must be 1-4\n" << kColorReset;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return;
  }

  session.SetBeat(beat, beat_within_bar);
  std::cout << kColorGreen << "✓ Beat set to " << beat << " (bar position: "
            << +beat_within_bar << "/4)\n" << kColorReset;
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

void HandleSetBeatWithinBar(prolink::Session& session) {
  std::cout << "\nEnter beat within bar (1-4): ";
  int temp;
  std::cin >> temp;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  uint8_t beat_within_bar = static_cast<uint8_t>(temp);

  if (beat_within_bar < 1 || beat_within_bar > 4) {
    std::cout << kColorRed << "Error: Beat within bar must be 1-4\n" << kColorReset;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return;
  }

  // Set beat to 1 with the new beat_within_bar
  session.SetBeat(1, beat_within_bar);
  std::cout << kColorGreen << "✓ Beat within bar set to " << +beat_within_bar << "/4\n"
            << kColorReset;
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

bool g_master = false;

void HandleToggleMaster(prolink::Session& session) {
  g_master = !g_master;
  session.SetMaster(g_master);

  if (g_master) {
    std::cout << kColorMagenta << "👑 Master mode enabled\n" << kColorReset;
  } else {
    std::cout << kColorYellow << "Slave mode enabled\n" << kColorReset;
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

bool g_synced = false;

void HandleToggleSync(prolink::Session& session) {
  g_synced = !g_synced;
  session.SetSynced(g_synced);

  if (g_synced) {
    std::cout << kColorCyan << "🔗 Sync enabled\n" << kColorReset;
  } else {
    std::cout << kColorYellow << "Sync disabled\n" << kColorReset;
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

void HandleRequestMaster(prolink::Session& session) {
  std::cout << "\nRequesting tempo master role...\n";
  session.RequestMasterRole();
  std::cout << kColorGreen << "✓ Master handoff request sent\n" << kColorReset;
  std::cout << "Watch for status changes from current master...\n";
  std::this_thread::sleep_for(std::chrono::seconds(2));
}

void HandleSyncCommand(prolink::Session& session) {
  auto devices = session.GetDevices();

  if (devices.empty()) {
    std::cout << kColorRed << "\nNo devices discovered yet!\n" << kColorReset;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return;
  }

  std::cout << "\nAvailable devices:\n";
  for (const auto& device : devices) {
    std::cout << "  [" << +device.device_number << "] " << device.device_name << "\n";
  }

  std::cout << "\nEnter target device number: ";
  int target;
  std::cin >> target;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  std::cout << "\nSync commands:\n";
  std::cout << "  1. Enable Sync\n";
  std::cout << "  2. Disable Sync\n";
  std::cout << "  3. Become Master\n";
  std::cout << "Enter command: ";
  int cmd;
  std::cin >> cmd;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  prolink::SyncCommand command;
  const char* cmd_name;

  switch (cmd) {
    case 1:
      command = prolink::SyncCommand::kEnableSync;
      cmd_name = "Enable Sync";
      break;
    case 2:
      command = prolink::SyncCommand::kDisableSync;
      cmd_name = "Disable Sync";
      break;
    case 3:
      command = prolink::SyncCommand::kBecomeMaster;
      cmd_name = "Become Master";
      break;
    default:
      std::cout << kColorRed << "Invalid command\n" << kColorReset;
      std::this_thread::sleep_for(std::chrono::seconds(2));
      return;
  }

  session.SendSyncControl(static_cast<uint8_t>(target), command);
  std::cout << kColorGreen << "✓ Sent '" << cmd_name << "' to device "
            << target << "\n" << kColorReset;
  std::this_thread::sleep_for(std::chrono::seconds(2));
}

void ShowCurrentState(prolink::Session& session) {
  ClearScreen();
  PrintHeader();
  PrintCurrentState(session);

  std::cout << "Press Enter to return to menu...";
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void ShowDevices(prolink::Session& session) {
  ClearScreen();
  PrintHeader();
  PrintDevices(session);

  std::cout << "Press Enter to return to menu...";
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cout << "Usage: " << argv[0]
              << " <device_ip> <broadcast_ip> <mac> [device_id] [name] [tempo]\n\n";
    std::cout << "Example:\n";
    std::cout << "  " << argv[0]
              << " 192.168.1.100 192.168.1.255 aa:bb:cc:dd:ee:ff 7 MyVirtualCDJ 128\n\n";
    std::cout << "Parameters:\n";
    std::cout << "  device_ip     - Your computer's IP address on the DJ network\n";
    std::cout << "  broadcast_ip  - Subnet broadcast address (e.g., 192.168.1.255)\n";
    std::cout << "  mac           - MAC address in format aa:bb:cc:dd:ee:ff\n";
    std::cout << "  device_id     - Optional device number (1-4 for players, 5+ for virtual)\n";
    std::cout << "  name          - Optional device name (default: VirtualCDJ)\n";
    std::cout << "  tempo         - Optional initial BPM (default: 128.0)\n";
    return 1;
  }

  prolink::Config config;
  config.device_ip = argv[1];
  config.broadcast_address = argv[2];
  config.announce_address = argv[2];

  if (!ParseMac(argv[3], &config.mac_address)) {
    std::cerr << "Error: Invalid MAC address format\n";
    return 1;
  }

  // Optional parameters
  if (argc > 4) {
    config.device_number = static_cast<uint8_t>(std::atoi(argv[4]));
  }
  if (argc > 5) {
    config.device_name = argv[5];
  } else {
    config.device_name = "VirtualCDJ";
  }
  if (argc > 6) {
    config.tempo_bpm = std::atof(argv[6]);
  }

  // Enable all features
  config.send_beats = true;
  config.send_status = true;
  config.send_announces = true;
  config.playing = false;  // Start stopped
  config.master = false;   // Start as slave
  config.synced = false;   // Start unsynced

  // Validate config
  std::string error;
  if (!config.Validate(&error)) {
    std::cerr << "Configuration error: " << error << "\n";
    return 1;
  }

  // Create and start session
  prolink::Session session(config);

  // Set up callbacks for monitoring
  session.SetBeatCallback([](const prolink::BeatInfo& beat) {
    // Silent - too noisy for interactive menu
  });

  session.SetStatusCallback([](const prolink::StatusInfo& status) {
    // Silent - too noisy for interactive menu
  });

  session.SetDeviceEventCallback([](const prolink::DeviceEvent& event) {
    if (event.type == prolink::DeviceEventType::kSeen) {
      std::cout << kColorGreen << "\n[New device: " << event.device.device_name
                << " (" << +event.device.device_number << ")]\n" << kColorReset;
    } else if (event.type == prolink::DeviceEventType::kExpired) {
      std::cout << kColorYellow << "\n[Device offline: " << event.device.device_name
                << " (" << +event.device.device_number << ")]\n" << kColorReset;
    }
  });

  if (!session.Start()) {
    std::cerr << "Failed to start session: " << session.GetLastError() << "\n";
    return 1;
  }

  // Wait a moment for initial device discovery
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Main interactive loop
  ClearScreen();
  PrintHeader();

  std::cout << kColorGreen << "✓ Virtual CDJ started successfully!\n" << kColorReset;
  std::cout << "  Device: " << config.device_name << " (ID: "
            << +config.device_number << ")\n";
  std::cout << "  Network: " << config.device_ip << " -> "
            << config.broadcast_address << "\n\n";

  std::cout << "Waiting for device discovery...\n";
  std::this_thread::sleep_for(std::chrono::seconds(3));

  while (g_running) {
    ClearScreen();
    PrintHeader();
    PrintCurrentState(session);
    PrintMenu();

    std::string choice;
    std::getline(std::cin, choice);

    if (choice.empty()) {
      continue;
    }

    char cmd = choice[0];

    try {
      switch (cmd) {
        case '1':
          HandleSetBpm(session);
          break;
        case '2':
          HandleSetPitch(session);
          break;
        case '3':
          HandleTogglePlaying(session);
          break;
        case '4':
          HandleSetBeat(session);
          break;
        case '5':
          HandleSetBeatWithinBar(session);
          break;
        case '6':
          HandleToggleMaster(session);
          break;
        case '7':
          HandleToggleSync(session);
          break;
        case '8':
          HandleRequestMaster(session);
          break;
        case '9':
          HandleSyncCommand(session);
          break;
        case 's':
        case 'S':
          ShowCurrentState(session);
          break;
        case 'd':
        case 'D':
          ShowDevices(session);
          break;
        case 'r':
        case 'R':
          // Just refresh (loop will clear and redraw)
          break;
        case 'h':
        case 'H':
          // Just show menu again (loop will redraw)
          break;
        case 'q':
        case 'Q':
          g_running = false;
          break;
        default:
          std::cout << kColorRed << "Invalid choice. Press 'h' for help.\n"
                    << kColorReset;
          std::this_thread::sleep_for(std::chrono::seconds(2));
          break;
      }
    } catch (const std::exception& e) {
      std::cout << kColorRed << "Error: " << e.what() << "\n" << kColorReset;
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }

  std::cout << "\n" << kColorYellow << "Shutting down virtual CDJ...\n" << kColorReset;
  session.Stop();
  std::cout << kColorGreen << "✓ Goodbye!\n" << kColorReset;

  return 0;
}
