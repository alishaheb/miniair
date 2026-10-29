#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "miniair/engine.hpp"
#include "miniair/geo.hpp"
#include "test_helpers.hpp"

using miniair::AlertEvent;
using miniair::AlertKind;
using miniair::Engine;
using miniair::EngineConfig;
using miniair::Severity;
using miniair::StateReport;
using miniair::test::aircraft_mps;

namespace {

StateReport report(const std::string& icao24, double east_m, double snapshot_s, double track_deg = 90.0) {
    return StateReport{aircraft_mps(icao24, east_m, 0.0, 35000.0, 250.0, track_deg, 0.0, snapshot_s), snapshot_s};
}

// Head-on pair: aaa001 flies east from 0 m, aaa002 flies west from 50 km.
std::vector<StateReport> head_on_snapshot(double t) {
    return {report("aaa001", 250.0 * t, t, 90.0), report("aaa002", 50000.0 - (250.0 * t), t, 270.0)};
}

}  // namespace

TEST(Engine, NoCycleRunsUntilTheSnapshotIsComplete) {
    Engine engine{EngineConfig{}};
    EXPECT_TRUE(engine.on_report(report("aaa001", 0.0, 100.0)).empty());
    EXPECT_TRUE(engine.on_report(report("aaa002", 90000.0, 100.0)).empty());
    EXPECT_EQ(engine.cycles_run(), 0U);

    (void)engine.on_report(report("aaa001", 1250.0, 105.0));  // Next snapshot starts
    EXPECT_EQ(engine.cycles_run(), 1U);
    EXPECT_DOUBLE_EQ(engine.last_cycle().time_s, 100.0);
    EXPECT_EQ(engine.last_cycle().tracks, 2U);
}

TEST(Engine, FlushRunsThePendingCycleOnce) {
    Engine engine{EngineConfig{}};
    EXPECT_TRUE(engine.flush().empty());
    EXPECT_EQ(engine.cycles_run(), 0U);  // Nothing to do yet

    (void)engine.on_report(report("aaa001", 0.0, 100.0));
    (void)engine.flush();
    EXPECT_EQ(engine.cycles_run(), 1U);
    (void)engine.flush();
    EXPECT_EQ(engine.cycles_run(), 1U);
}

TEST(Engine, LateReportDoesNotMoveTheClockBackwards) {
    Engine engine{EngineConfig{}};
    (void)engine.on_report(report("aaa001", 0.0, 100.0));
    (void)engine.on_report(report("aaa001", 1250.0, 105.0));
    (void)engine.flush();
    EXPECT_DOUBLE_EQ(engine.last_cycle().time_s, 105.0);

    (void)engine.on_report(report("aaa002", 0.0, 100.0));  // Arrives late
    (void)engine.flush();
    EXPECT_DOUBLE_EQ(engine.last_cycle().time_s, 105.0);
    EXPECT_EQ(engine.last_cycle().tracks, 2U);
}

TEST(Engine, StaleTracksAreDropped) {
    EngineConfig config;
    config.max_track_age_s = 30.0;
    Engine engine{config};
    (void)engine.on_report(report("gone", 0.0, 0.0));
    for (double t = 0.0; t <= 60.0; t += 10.0) {
        (void)engine.on_report(report("stays", 0.0, t));
    }
    (void)engine.flush();
    EXPECT_EQ(engine.last_cycle().tracks, 1U);
}

namespace {

// The case found in live OpenSky data: FL380 and FL390 crossing, the lower
// aircraft reporting +65 ft/min (one ADS-B step of noise), with its position
// fix 1 s older than the snapshot.
std::vector<AlertEvent> run_noisy_level_pair(const EngineConfig& config, double lower_vrate_fpm) {
    constexpr double kMpsPerFpm = 0.3048 / 60.0;
    StateReport lower{miniair::test::aircraft("aaa001", 0.0, 0.0, 38000.0, 450.0, 90.0, lower_vrate_fpm, 99.0),
                      100.0};
    StateReport upper{miniair::test::aircraft("aaa002", 30000.0, 0.0, 39000.0, 450.0, 270.0, 0.0, 100.0), 100.0};
    lower.state.vertical_rate_mps = lower_vrate_fpm * kMpsPerFpm;

    Engine engine{config};
    (void)engine.on_report(lower);
    (void)engine.on_report(upper);
    return engine.flush();
}

}  // namespace

TEST(Engine, AdsbVerticalRateNoiseDoesNotRaiseFalseAlerts) {
    EXPECT_TRUE(run_noisy_level_pair(EngineConfig{}, 65.0).empty());
}

TEST(Engine, WithoutTheLevelBandTheSameNoiseRaisesAnAlert) {
    // Shows why the band exists: 1 s of extrapolation at +65 ft/min puts the
    // pair 998.9 ft apart, which is inside the 1000 ft minimum.
    EngineConfig no_band;
    no_band.level_flight_fpm = 0.0;
    EXPECT_EQ(run_noisy_level_pair(no_band, 65.0).size(), 1U);
}

TEST(Engine, RealClimbAboveTheLevelBandIsStillPredicted) {
    // 500 ft/min is a real, if slow, climb: 1000 ft in 2 minutes.
    const std::vector<AlertEvent> events = run_noisy_level_pair(EngineConfig{}, 500.0);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, AlertKind::New);
    EXPECT_EQ(events[0].conflict.severity, Severity::Predicted);
}

TEST(Engine, HeadOnEncounterGoesNewThenUpdatedThenCleared) {
    Engine engine{EngineConfig{}};
    std::vector<AlertEvent> events;
    for (double t = 0.0; t <= 200.0; t += 5.0) {
        for (const StateReport& r : head_on_snapshot(t)) {
            for (AlertEvent& e : engine.on_report(r)) {
                events.push_back(e);
            }
        }
    }
    for (AlertEvent& e : engine.flush()) {
        events.push_back(e);
    }

    // Separation is lost at 81.5 s, they pass at 100 s and are 5 NM apart
    // again at 118.5 s. With 2 clear cycles, CLEARED comes at 125 s.
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].kind, AlertKind::New);
    EXPECT_EQ(events[0].conflict.severity, Severity::Predicted);
    EXPECT_DOUBLE_EQ(events[0].cycle_time_s, 0.0);

    EXPECT_EQ(events[1].kind, AlertKind::Updated);
    EXPECT_EQ(events[1].conflict.severity, Severity::LossOfSeparation);
    EXPECT_DOUBLE_EQ(events[1].cycle_time_s, 85.0);

    EXPECT_EQ(events[2].kind, AlertKind::Cleared);
    EXPECT_DOUBLE_EQ(events[2].cycle_time_s, 125.0);
    EXPECT_EQ(engine.active_alerts(), 0U);
}
