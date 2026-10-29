#include <gtest/gtest.h>

#include "miniair/geo.hpp"
#include "miniair/track_store.hpp"
#include "test_helpers.hpp"

using miniair::AircraftState;
using miniair::TrackStore;
using miniair::test::aircraft_mps;

TEST(TrackStore, StoresNewAircraft) {
    TrackStore store;
    EXPECT_TRUE(store.update(aircraft_mps("aaa001", 0.0, 0.0, 35000.0, 200.0, 90.0, 0.0, 100.0)));
    EXPECT_TRUE(store.update(aircraft_mps("aaa002", 0.0, 0.0, 35000.0, 200.0, 90.0, 0.0, 100.0)));
    EXPECT_EQ(store.size(), 2U);
}

TEST(TrackStore, NewerReportReplacesOlder) {
    TrackStore store;
    (void)store.update(aircraft_mps("aaa001", 0.0, 0.0, 35000.0, 200.0, 90.0, 0.0, 100.0));
    EXPECT_TRUE(store.update(aircraft_mps("aaa001", 0.0, 0.0, 36000.0, 200.0, 90.0, 0.0, 110.0)));

    const std::vector<AircraftState> picture = store.picture_at(110.0);
    ASSERT_EQ(picture.size(), 1U);
    EXPECT_NEAR(picture[0].alt_m, miniair::geo::ft_to_m(36000.0), 1e-9);
}

TEST(TrackStore, OlderReportIsIgnored) {
    TrackStore store;
    (void)store.update(aircraft_mps("aaa001", 0.0, 0.0, 36000.0, 200.0, 90.0, 0.0, 110.0));
    EXPECT_FALSE(store.update(aircraft_mps("aaa001", 0.0, 0.0, 35000.0, 200.0, 90.0, 0.0, 100.0)));

    const std::vector<AircraftState> picture = store.picture_at(110.0);
    ASSERT_EQ(picture.size(), 1U);
    EXPECT_NEAR(picture[0].alt_m, miniair::geo::ft_to_m(36000.0), 1e-9);
}

TEST(TrackStore, PruneRemovesOnlyStaleTracks) {
    TrackStore store;
    (void)store.update(aircraft_mps("old", 0.0, 0.0, 35000.0, 200.0, 90.0, 0.0, 0.0));
    (void)store.update(aircraft_mps("fresh", 0.0, 0.0, 35000.0, 200.0, 90.0, 0.0, 50.0));

    EXPECT_EQ(store.prune(70.0, 60.0), 1U);
    EXPECT_EQ(store.size(), 1U);
    EXPECT_EQ(store.picture_at(70.0)[0].icao24, "fresh");
}

TEST(TrackStore, PruneKeepsTrackExactlyAtMaxAge) {
    TrackStore store;
    (void)store.update(aircraft_mps("edge", 0.0, 0.0, 35000.0, 200.0, 90.0, 0.0, 10.0));
    EXPECT_EQ(store.prune(70.0, 60.0), 0U);
}

TEST(TrackStore, PictureMovesEveryTrackToTheSameTime) {
    TrackStore store;
    const AircraftState a = aircraft_mps("aaa001", 0.0, 0.0, 35000.0, 100.0, 0.0, 0.0, 100.0);
    const AircraftState b = aircraft_mps("aaa002", 0.0, 0.0, 35000.0, 100.0, 0.0, 0.0, 104.0);
    (void)store.update(a);
    (void)store.update(b);

    for (const AircraftState& s : store.picture_at(110.0)) {
        EXPECT_DOUBLE_EQ(s.time_s, 110.0);
        const double expected_m = (s.icao24 == "aaa001") ? 1000.0 : 600.0;
        EXPECT_NEAR(miniair::geo::haversine_m(a.lat_deg, a.lon_deg, s.lat_deg, s.lon_deg), expected_m, 0.1);
    }
}
