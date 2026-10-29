#include <gtest/gtest.h>

#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "miniair/conflict_detector.hpp"
#include "miniair/geo.hpp"
#include "test_helpers.hpp"

using miniair::AircraftState;
using miniair::Conflict;
using miniair::ConflictDetector;
using miniair::DetectorConfig;
using miniair::Severity;
using miniair::test::aircraft;
using miniair::test::aircraft_mps;
namespace geo = miniair::geo;

namespace {

constexpr double kFiveNmM = 9260.0;

DetectorConfig config_with_look_ahead(double look_ahead_s) {
    DetectorConfig config;
    config.look_ahead_s = look_ahead_s;
    return config;
}

}  // namespace

// ---- Single pair geometry -------------------------------------------------

TEST(ConflictDetector, HeadOnAtSameLevelIsPredicted) {
    // 50 km apart, closing at 500 m/s. 5 NM is reached after (50000 - 9260) / 500 s.
    const ConflictDetector detector{DetectorConfig{}};
    const AircraftState a = aircraft_mps("aaa001", 0.0, 0.0, 35000.0, 250.0, 90.0);
    const AircraftState b = aircraft_mps("aaa002", 50000.0, 0.0, 35000.0, 250.0, 270.0);

    const auto conflict = detector.check_pair(a, b);
    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(conflict->severity, Severity::Predicted);
    EXPECT_NEAR(conflict->time_to_conflict_s, (50000.0 - kFiveNmM) / 500.0, 0.01);
    EXPECT_NEAR(conflict->cpa_time_s, 100.0, 0.01);
    EXPECT_NEAR(conflict->cpa_horizontal_m, 0.0, 1.0);
    EXPECT_NEAR(conflict->cpa_vertical_m, 0.0, 1e-9);
    EXPECT_NEAR(conflict->current_horizontal_m, 50000.0, 1.0);
}

TEST(ConflictDetector, RightAngleCrossingIsPredicted) {
    // Both 50 km from the crossing point at 230 m/s: they arrive together at
    // 217 s. The look-ahead must reach past that for the CPA to be inside it.
    const ConflictDetector detector{config_with_look_ahead(240.0)};
    const AircraftState east = aircraft_mps("aaa001", -50000.0, 0.0, 35000.0, 230.0, 90.0);
    const AircraftState north = aircraft_mps("aaa002", 0.0, -50000.0, 35000.0, 230.0, 0.0);

    const auto conflict = detector.check_pair(east, north);
    ASSERT_TRUE(conflict.has_value());
    // |r + v t| = sqrt(2) * (50000 - 230 t) reaches 9260 m at t = 188.9 s.
    const double expected = (50000.0 - (kFiveNmM / std::sqrt(2.0))) / 230.0;
    EXPECT_NEAR(conflict->time_to_conflict_s, expected, 1.5);  // Plane approximation
    EXPECT_NEAR(conflict->cpa_time_s, 50000.0 / 230.0, 1.5);
    EXPECT_NEAR(conflict->cpa_horizontal_m, 0.0, 500.0);
}

TEST(ConflictDetector, CpaIsLimitedToTheLookAheadWindow) {
    // Same crossing with a 200 s look-ahead: the true CPA (217 s) is beyond
    // the horizon, so the reported CPA is the closest point within it.
    const ConflictDetector detector{config_with_look_ahead(200.0)};
    const AircraftState east = aircraft_mps("aaa001", -50000.0, 0.0, 35000.0, 230.0, 90.0);
    const AircraftState north = aircraft_mps("aaa002", 0.0, -50000.0, 35000.0, 230.0, 0.0);

    const auto conflict = detector.check_pair(east, north);
    ASSERT_TRUE(conflict.has_value());
    EXPECT_DOUBLE_EQ(conflict->cpa_time_s, 200.0);
    EXPECT_NEAR(conflict->cpa_horizontal_m, std::sqrt(2.0) * (50000.0 - (230.0 * 200.0)), 250.0);
}

TEST(ConflictDetector, PairOrderDoesNotMatter) {
    const ConflictDetector detector{DetectorConfig{}};
    const AircraftState a = aircraft_mps("aaa001", 0.0, 0.0, 35000.0, 250.0, 90.0);
    const AircraftState b = aircraft_mps("aaa002", 50000.0, 0.0, 35000.0, 250.0, 270.0);

    const auto forward = detector.check_pair(a, b);
    const auto backward = detector.check_pair(b, a);
    ASSERT_TRUE(forward.has_value());
    ASSERT_TRUE(backward.has_value());
    EXPECT_EQ(backward->icao24_a, "aaa001");
    EXPECT_EQ(backward->icao24_b, "aaa002");
    EXPECT_NEAR(forward->time_to_conflict_s, backward->time_to_conflict_s, 1e-9);
}

TEST(ConflictDetector, AlreadyTooCloseIsLossOfSeparation) {
    // Side by side, 3 NM apart, same speed and level: never separates.
    const ConflictDetector detector{DetectorConfig{}};
    const AircraftState a = aircraft("aaa001", 0.0, 0.0, 35000.0, 450.0, 0.0);
    const AircraftState b = aircraft("aaa002", geo::nm_to_m(3.0), 0.0, 35500.0, 450.0, 0.0);

    const auto conflict = detector.check_pair(a, b);
    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(conflict->severity, Severity::LossOfSeparation);
    EXPECT_DOUBLE_EQ(conflict->time_to_conflict_s, 0.0);
    EXPECT_NEAR(conflict->current_horizontal_m, geo::nm_to_m(3.0), 1.0);
    EXPECT_NEAR(conflict->current_vertical_m, geo::ft_to_m(500.0), 1e-6);
}

TEST(ConflictDetector, VerticallySeparatedCrossingIsNotAConflict) {
    const ConflictDetector detector{DetectorConfig{}};
    const AircraftState a = aircraft_mps("aaa001", -20000.0, 0.0, 35000.0, 230.0, 90.0);
    const AircraftState b = aircraft_mps("aaa002", 0.0, -20000.0, 37000.0, 230.0, 0.0);
    EXPECT_FALSE(detector.check_pair(a, b).has_value());
}

TEST(ConflictDetector, ExactlyOneThousandFeetApartIsSeparated) {
    // FL350 and FL360 directly above each other: legal RVSM separation.
    // Guards against floating-point rounding raising a false alert.
    const ConflictDetector detector{DetectorConfig{}};
    const AircraftState a = aircraft("aaa001", 0.0, 0.0, 35000.0, 450.0, 90.0);
    const AircraftState b = aircraft("aaa002", 0.0, 0.0, 36000.0, 450.0, 270.0);
    EXPECT_FALSE(detector.check_pair(a, b).has_value());
}

TEST(ConflictDetector, NineHundredFeetApartIsLossOfSeparation) {
    const ConflictDetector detector{DetectorConfig{}};
    const AircraftState a = aircraft("aaa001", 0.0, 0.0, 35000.0, 450.0, 90.0);
    const AircraftState b = aircraft("aaa002", 1000.0, 0.0, 35900.0, 450.0, 270.0);
    const auto conflict = detector.check_pair(a, b);
    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(conflict->severity, Severity::LossOfSeparation);
}

TEST(ConflictDetector, ParallelTracksTenMilesApartAreNotAConflict) {
    const ConflictDetector detector{DetectorConfig{}};
    const AircraftState a = aircraft("aaa001", 0.0, 0.0, 35000.0, 450.0, 0.0);
    const AircraftState b = aircraft("aaa002", geo::nm_to_m(10.0), 0.0, 35000.0, 450.0, 0.0);
    EXPECT_FALSE(detector.check_pair(a, b).has_value());
}

TEST(ConflictDetector, DivergingAircraftAreNotAConflict) {
    // 6 NM apart and flying away from each other: the closest point is behind them.
    const ConflictDetector detector{DetectorConfig{}};
    const AircraftState a = aircraft("aaa001", 0.0, 0.0, 35000.0, 450.0, 270.0);
    const AircraftState b = aircraft("aaa002", geo::nm_to_m(6.0), 0.0, 35000.0, 450.0, 90.0);
    EXPECT_FALSE(detector.check_pair(a, b).has_value());
}

TEST(ConflictDetector, ConflictBeyondLookAheadIsIgnored) {
    // Head-on, 100 km apart: 5 NM is reached after 181.5 s.
    const AircraftState a = aircraft_mps("aaa001", 0.0, 0.0, 35000.0, 250.0, 90.0);
    const AircraftState b = aircraft_mps("aaa002", 100000.0, 0.0, 35000.0, 250.0, 270.0);

    EXPECT_FALSE(ConflictDetector{config_with_look_ahead(120.0)}.check_pair(a, b).has_value());

    const auto later = ConflictDetector{config_with_look_ahead(200.0)}.check_pair(a, b);
    ASSERT_TRUE(later.has_value());
    EXPECT_NEAR(later->time_to_conflict_s, (100000.0 - kFiveNmM) / 500.0, 0.01);
}

TEST(ConflictDetector, ClimbThroughUsesTheVerticalEntryTime) {
    // Horizontal minimum is lost at 92.6 s, but the climbing aircraft only
    // gets within 1000 ft at 120 s. The conflict starts at the later time.
    const ConflictDetector detector{config_with_look_ahead(180.0)};
    const AircraftState climber = aircraft_mps("aaa001", 0.0, 0.0, 30000.0, 220.0, 90.0, 7.62);
    const AircraftState level = aircraft_mps("aaa002", 50000.0, 0.0, 34000.0, 220.0, 270.0, 0.0);

    const auto conflict = detector.check_pair(climber, level);
    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(conflict->severity, Severity::Predicted);
    EXPECT_NEAR(conflict->time_to_conflict_s, 120.0, 0.1);
    EXPECT_NEAR(conflict->cpa_time_s, 120.0, 0.1);  // Clamped into the conflict window
    // They passed each other at 113.6 s, so they are now 2800 m apart.
    EXPECT_NEAR(conflict->cpa_horizontal_m, std::fabs(50000.0 - (440.0 * 120.0)), 25.0);
}

TEST(ConflictDetector, ClimbThroughIsMissedWhenLookAheadIsTooShort) {
    const ConflictDetector detector{config_with_look_ahead(100.0)};
    const AircraftState climber = aircraft_mps("aaa001", 0.0, 0.0, 30000.0, 220.0, 90.0, 7.62);
    const AircraftState level = aircraft_mps("aaa002", 50000.0, 0.0, 34000.0, 220.0, 270.0, 0.0);
    EXPECT_FALSE(detector.check_pair(climber, level).has_value());
}

TEST(ConflictDetector, CustomMinimaAreApplied) {
    // 4 NM apart side by side: a conflict at 5 NM, fine at 3 NM (terminal area).
    const AircraftState a = aircraft("aaa001", 0.0, 0.0, 5000.0, 200.0, 0.0);
    const AircraftState b = aircraft("aaa002", geo::nm_to_m(4.0), 0.0, 5000.0, 200.0, 0.0);
    DetectorConfig terminal;
    terminal.horizontal_nm = 3.0;

    EXPECT_TRUE(ConflictDetector{DetectorConfig{}}.check_pair(a, b).has_value());
    EXPECT_FALSE(ConflictDetector{terminal}.check_pair(a, b).has_value());
}

// ---- Whole picture ---------------------------------------------------------

TEST(ConflictDetector, DetectIgnoresAircraftOnTheGroundOrTooLow) {
    const ConflictDetector detector{DetectorConfig{}};
    AircraftState taxiing = aircraft("aaa001", 0.0, 0.0, 0.0, 15.0, 90.0);
    taxiing.on_ground = true;
    const AircraftState low = aircraft("aaa002", 200.0, 0.0, 500.0, 140.0, 270.0);
    const AircraftState also_low = aircraft("aaa003", 400.0, 0.0, 600.0, 140.0, 270.0);

    EXPECT_FALSE(detector.is_eligible(taxiing));
    EXPECT_FALSE(detector.is_eligible(low));
    EXPECT_TRUE(detector.detect({taxiing, low, also_low}).empty());
}

TEST(ConflictDetector, DetectReturnsOnlyConflictingPairsInOrder) {
    const ConflictDetector detector{DetectorConfig{}};
    const std::vector<AircraftState> picture{
        aircraft_mps("ccc003", 50000.0, 0.0, 35000.0, 250.0, 270.0),       // Head-on with aaa001
        aircraft_mps("bbb002", 0.0, 200000.0, 35000.0, 250.0, 90.0),       // Far to the north
        aircraft_mps("aaa001", 0.0, 0.0, 35000.0, 250.0, 90.0),
        aircraft_mps("ddd004", 50000.0, 0.0, 39000.0, 250.0, 270.0),       // Same place, 4000 ft above
    };
    const std::vector<Conflict> conflicts = detector.detect(picture);
    ASSERT_EQ(conflicts.size(), 1U);
    EXPECT_EQ(conflicts[0].icao24_a, "aaa001");
    EXPECT_EQ(conflicts[0].icao24_b, "ccc003");
}

TEST(ConflictDetector, DetectOnEmptyPictureIsEmpty) {
    EXPECT_TRUE(ConflictDetector{DetectorConfig{}}.detect({}).empty());
}

TEST(ConflictDetector, LatitudeSweepFindsTheSameConflictsAsBruteForce) {
    // The sweep skips pairs that are too far apart north-south. This checks
    // that skipping never loses a real conflict, on a dense random picture.
    std::mt19937 rng(20251028U);
    std::uniform_real_distribution<double> offset_m(-150000.0, 150000.0);
    std::uniform_real_distribution<double> alt_ft(30000.0, 34000.0);
    std::uniform_real_distribution<double> speed_mps(120.0, 260.0);
    std::uniform_real_distribution<double> track(0.0, 360.0);
    std::uniform_real_distribution<double> vrate(-10.0, 10.0);

    std::vector<AircraftState> picture;
    for (int i = 0; i < 400; ++i) {
        picture.push_back(aircraft_mps("t" + std::to_string(i), offset_m(rng), offset_m(rng), alt_ft(rng),
                                       speed_mps(rng), track(rng), vrate(rng)));
    }

    const ConflictDetector detector{DetectorConfig{}};
    std::set<std::pair<std::string, std::string>> brute_force;
    for (std::size_t i = 0; i < picture.size(); ++i) {
        for (std::size_t j = i + 1; j < picture.size(); ++j) {
            const auto conflict = detector.check_pair(picture[i], picture[j]);
            if (conflict.has_value()) {
                brute_force.emplace(conflict->icao24_a, conflict->icao24_b);
            }
        }
    }
    std::set<std::pair<std::string, std::string>> swept;
    for (const Conflict& c : detector.detect(picture)) {
        swept.emplace(c.icao24_a, c.icao24_b);
    }

    ASSERT_GT(brute_force.size(), 5U) << "Test picture is too sparse to be meaningful";
    EXPECT_EQ(swept, brute_force);
}

TEST(SeverityNames, AreStable) {
    EXPECT_STREQ(miniair::to_string(Severity::Predicted), "PREDICTED");
    EXPECT_STREQ(miniair::to_string(Severity::LossOfSeparation), "LOSS_OF_SEPARATION");
}
