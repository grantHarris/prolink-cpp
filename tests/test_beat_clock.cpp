// Tests for beat clock alignment and timing.
#include "prolink/test_hooks.h"

#include <gtest/gtest.h>

TEST(BeatClockTest, CalculatesCorrectIntervals) {
  prolink::test::BeatClockTester clock(4);
  clock.SetTempo(120.0);
  clock.SetPlaying(false);

  const auto now = std::chrono::steady_clock::now();
  const auto snapshot = clock.Snapshot(now);
  EXPECT_NEAR(snapshot.beat_interval_ms, 500.0, 0.5);
  EXPECT_NEAR(snapshot.bar_interval_ms, 2000.0, 1.0);
}

TEST(BeatClockTest, AlignmentToBeatZeroBecomesOne) {
  prolink::test::BeatClockTester clock(4);
  const auto now = std::chrono::steady_clock::now();
  clock.AlignToBeatNumber(0, 0, now);

  const auto snapshot = clock.Snapshot(now);
  EXPECT_EQ(snapshot.beat, 1u);
  EXPECT_EQ(snapshot.beat_within_bar, 1);
}

TEST(BeatClockTest, PlayingAdvancesBeat) {
  prolink::test::BeatClockTester clock(4);
  clock.SetTempo(120.0);
  clock.SetPlaying(true);

  const auto start = std::chrono::steady_clock::now();
  clock.AlignToBeatNumber(1, 1, start);

  const auto later = start + std::chrono::milliseconds(500);
  const auto snapshot = clock.Snapshot(later);
  EXPECT_EQ(snapshot.beat, 2u);
  EXPECT_EQ(snapshot.beat_within_bar, 2);
}

TEST(BeatClockTest, AlignToBeatWithinBarWraps) {
  prolink::test::BeatClockTester clock(4);
  const auto now = std::chrono::steady_clock::now();
  clock.AlignToBeatNumber(1, 1, now);
  clock.AlignToBeatWithinBar(4, now);

  const auto snapshot = clock.Snapshot(now);
  EXPECT_EQ(snapshot.beat_within_bar, 4);
}

TEST(BeatClockTest, TempoZeroDefaultsTo120) {
  prolink::test::BeatClockTester clock(4);
  clock.SetTempo(0.0);
  clock.SetPlaying(false);

  const auto now = std::chrono::steady_clock::now();
  const auto snapshot = clock.Snapshot(now);
  EXPECT_NEAR(snapshot.tempo_bpm, 120.0, 0.1);
}
