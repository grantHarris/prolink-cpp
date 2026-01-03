// Example: send announce/status/beat packets as a virtual CDJ.
#include "prolink/prolink.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cout << "Usage: prolink_virtual_cdj <device_ip> <broadcast_ip> <mac> "
                 "[device_id] [name] [tempo] [--follow-master]\n";
    return 1;
  }

  prolink::Config config;
  config.device_ip = argv[1];
  config.broadcast_address = argv[2];
  config.announce_address = argv[2];

  if (!ParseMac(argv[3], &config.mac_address)) {
    std::cerr << "Invalid MAC address format\n";
    return 1;
  }

  for (int i = 4; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--follow-master") {
      config.follow_master = true;
      config.synced = true;
      continue;
    }
    if (config.device_number == 0x07) {
      config.device_number = static_cast<uint8_t>(std::atoi(arg.c_str()));
    } else if (config.device_name == "prolink-cpp") {
      config.device_name = arg;
    } else if (config.tempo_bpm == 120.0) {
      config.tempo_bpm = std::atof(arg.c_str());
    }
  }

  config.playing = true;
  config.master = !config.follow_master;
  config.send_beats = true;
  config.send_status = true;
  config.send_announces = true;

  prolink::Session session(config);
  if (!session.Start()) {
    std::cerr << "Failed to start session: " << session.GetLastError() << std::endl;
    return 1;
  }
  std::cout << "Virtual CDJ running. Press Enter to stop." << std::endl;
  std::string line;
  std::getline(std::cin, line);
  session.Stop();
  return 0;
}
