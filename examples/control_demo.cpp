// Example: discover devices and exercise sync/master control.
#include "prolink/prolink.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main() {
  prolink::Config config;
  config.send_beats = false;
  config.send_status = false;
  config.send_announces = false;

  prolink::Session session(config);
  session.SetDeviceEventCallback([](const prolink::DeviceEvent& event) {
    const auto& device = event.device;
    const char* type = "seen";
    switch (event.type) {
      case prolink::DeviceEventType::kSeen:
        type = "seen";
        break;
      case prolink::DeviceEventType::kUpdated:
        type = "updated";
        break;
      case prolink::DeviceEventType::kExpired:
        type = "expired";
        break;
    }
    std::cout << "device " << type << ": " << device.device_name << " ("
              << +device.device_number << ") ip=" << device.ip_address << "\n";
  });

  if (!session.Start()) {
    std::cerr << "Failed to start session: " << session.GetLastError() << std::endl;
    return 1;
  }

  std::cout << "Waiting for devices (5s)..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  const auto devices = session.GetDevices();
  std::cout << "Discovered devices: " << devices.size() << std::endl;
  for (const auto& device : devices) {
    std::cout << " - " << device.device_name << " (" << +device.device_number
              << ") ip=" << device.ip_address << std::endl;
  }

  if (!devices.empty()) {
    const auto target = devices.front().device_number;
    std::cout << "Sending sync ON to device " << +target << std::endl;
    session.SendSyncControl(target, prolink::SyncCommand::kEnableSync);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "Sending sync OFF to device " << +target << std::endl;
    session.SendSyncControl(target, prolink::SyncCommand::kDisableSync);
  }

  std::cout << "Requesting master role" << std::endl;
  session.RequestMasterRole();

  std::cout << "Press Enter to stop." << std::endl;
  std::string line;
  std::getline(std::cin, line);
  session.Stop();
  return 0;
}
