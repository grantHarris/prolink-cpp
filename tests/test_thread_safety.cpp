// Thread safety smoke tests for concurrent setters.
#include "prolink/prolink.h"

#include <gtest/gtest.h>
#include <thread>

TEST(ThreadSafetyTest, ConcurrentStateUpdatesAreSafe) {
  prolink::Config config;
  prolink::Session session(config);

  std::thread t1([&]() {
    for (int i = 0; i < 1000; ++i) {
      session.SetTempo(120.0 + (i % 5));
    }
  });
  std::thread t2([&]() {
    for (int i = 0; i < 1000; ++i) {
      session.SetPitchPercent((i % 3) * 1.5);
    }
  });
  std::thread t3([&]() {
    for (int i = 0; i < 1000; ++i) {
      session.SetPlaying((i % 2) == 0);
    }
  });

  t1.join();
  t2.join();
  t3.join();

  SUCCEED();
}
