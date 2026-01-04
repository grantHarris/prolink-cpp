// Example: listen for beat and status packets on the Pro DJ Link network.
#include "prolink/prolink.h"

#include <iomanip>
#include <iostream>
#include <string>

int main() {
  prolink::Config config;
  config.send_beats = false;
  config.send_status = false;
  config.send_announces = false;

  prolink::Session session(config);
  std::cout << std::fixed << std::setprecision(2);

  session.SetBeatCallback([](const prolink::BeatInfo& beat) {
    const double track_bpm = beat.bpm / 100.0;
    const double effective_bpm = beat.effective_bpm();
    const double pitch_percent =
        (static_cast<double>(beat.pitch) / prolink::kNeutralPitch - 1.0) * 100.0;
    std::cout << "Beat from " << beat.device_name << " (" << +beat.device_number
              << ") bpm=" << track_bpm
              << " eff=" << effective_bpm
              << " pitch=" << std::showpos << pitch_percent << std::noshowpos << "%";
    std::cout << " pitch_raw=0x" << std::hex << beat.pitch << std::dec;
    std::cout << " beat=" << +beat.beat_within_bar << std::endl;
  });
  session.SetStatusCallback([](const prolink::StatusInfo& status) {
    const double pitch_percent =
        (static_cast<double>(status.pitch) / prolink::kNeutralPitch - 1.0) * 100.0;
    std::cout << "Status from " << status.device_name << " (" << +status.device_number
              << ") master=" << (status.is_master ? "y" : "n")
              << " synced=" << (status.is_synced ? "y" : "n")
              << " playing=" << (status.is_playing ? "y" : "n")
              << " pitch=" << std::showpos << pitch_percent << std::noshowpos << "%";
    std::cout << " pitch_raw=0x" << std::hex << status.pitch << std::dec;
    if (status.bpm.has_value()) {
      std::cout << " bpm=" << (status.bpm.value() / 100.0);
      const auto effective = status.effective_bpm();
      if (effective.has_value()) {
        std::cout << " eff=" << effective.value();
      }
    }
    std::cout << std::endl;
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
