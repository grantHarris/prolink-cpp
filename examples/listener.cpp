// Example: listen for beat and status packets on the Pro DJ Link network.
#include "prolink/prolink.h"

#include <iostream>
#include <string>

int main() {
  prolink::Config config;
  config.send_beats = false;
  config.send_status = false;
  config.send_announces = false;

  prolink::Session session(config);
  session.SetBeatCallback([](const prolink::BeatInfo& beat) {
    std::cout << "Beat from " << beat.device_name << " (" << +beat.device_number
              << ") bpm=" << (beat.bpm / 100.0)
              << " beat=" << +beat.beat_within_bar << std::endl;
  });
  session.SetStatusCallback([](const prolink::StatusInfo& status) {
    std::cout << "Status from " << status.device_name << " (" << +status.device_number
              << ") master=" << (status.is_master ? "y" : "n")
              << " synced=" << (status.is_synced ? "y" : "n")
              << " playing=" << (status.is_playing ? "y" : "n")
              << std::endl;
  });

  if (!session.Start()) {
    std::cerr << "Failed to start session: " << session.GetLastError() << std::endl;
    return 1;
  }
  std::cout << "Listening. Press Enter to stop." << std::endl;
  std::string line;
  std::getline(std::cin, line);
  session.Stop();
  return 0;
}
