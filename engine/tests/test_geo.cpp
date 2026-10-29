#include <gtest/gtest.h>

#include "miniair/geo.hpp"
#include "test_helpers.hpp"

namespace geo = miniair::geo;
using miniair::AircraftState;

TEST(Geo, UnitConversions) {
    EXPECT_DOUBLE_EQ(geo::nm_to_m(1.0), 1852.0);
    EXPECT_DOUBLE_EQ(geo::ft_to_m(1000.0), 304.8);
    EXPECT_NEAR(geo::m_to_nm(geo::nm_to_m(5.0)), 5.0, 1e-12);
    EXPECT_NEAR(geo::m_to_ft(geo::ft_to_m(35000.0)), 35000.0, 1e-9);
    EXPECT_NEAR(geo::rad_to_deg(geo::deg_to_rad(123.4)), 123.4, 1e-12);
}

TEST(Geo, HaversineOneDegreeOfLatitude) {
    EXPECT_NEAR(geo::haversine_m(0.0, 0.0, 1.0, 0.0), 111194.93, 0.01);
}

TEST(Geo, HaversineOneDegreeOfLongitudeShrinksWithLatitude) {
    EXPECT_NEAR(geo::haversine_m(60.0, 0.0, 60.0, 1.0), 55596.93, 0.01);
}

TEST(Geo, HaversineHeathrowToCharlesDeGaulle) {
    // Reference value computed independently with the same Earth radius.
    EXPECT_NEAR(geo::haversine_m(51.4700, -0.4543, 49.0097, 2.5479), 346965.0, 1.0);
}

TEST(Geo, HaversineAcrossTheAntimeridian) {
    EXPECT_NEAR(geo::haversine_m(0.0, 179.5, 0.0, -179.5), 111194.93, 0.01);
}

TEST(Geo, HaversineIsZeroForTheSamePoint) {
    EXPECT_DOUBLE_EQ(geo::haversine_m(52.0, -1.0, 52.0, -1.0), 0.0);
}

TEST(Geo, LocalPlaneAgreesWithHaversineAtConflictRanges) {
    // Pairs from about 10 km to 100 km apart, the ranges detection cares about.
    const double offsets_deg[] = {0.05, 0.2, 0.5};
    for (const double d : offsets_deg) {
        const double lat2 = 52.0 + d;
        const double lon2 = -1.0 + (1.5 * d);
        const geo::Vec2 r = geo::relative_position_m(52.0, -1.0, lat2, lon2);
        const double plane = std::sqrt((r.x * r.x) + (r.y * r.y));
        const double sphere = geo::haversine_m(52.0, -1.0, lat2, lon2);
        EXPECT_LT(std::fabs(plane - sphere) / sphere, 0.001) << "offset " << d << " deg";
        EXPECT_GT(r.x, 0.0);
        EXPECT_GT(r.y, 0.0);
    }
}

TEST(Geo, RelativePositionAcrossTheAntimeridian) {
    const geo::Vec2 r = geo::relative_position_m(0.0, 179.9, 0.0, -179.9);
    EXPECT_NEAR(r.x, geo::haversine_m(0.0, 179.9, 0.0, -179.9), 1.0);  // Eastwards, short way round
    EXPECT_NEAR(r.y, 0.0, 1e-9);
}

TEST(Geo, VelocityComponentsFollowCompassConvention) {
    const geo::Vec2 north = geo::velocity_mps(100.0, 0.0);
    EXPECT_NEAR(north.x, 0.0, 1e-9);
    EXPECT_NEAR(north.y, 100.0, 1e-9);

    const geo::Vec2 east = geo::velocity_mps(100.0, 90.0);
    EXPECT_NEAR(east.x, 100.0, 1e-9);
    EXPECT_NEAR(east.y, 0.0, 1e-9);

    const geo::Vec2 south_west = geo::velocity_mps(100.0, 225.0);
    EXPECT_NEAR(south_west.x, -70.7107, 1e-4);
    EXPECT_NEAR(south_west.y, -70.7107, 1e-4);
}

TEST(Geo, ExtrapolateNorthboundClimbing) {
    AircraftState s = miniair::test::aircraft_mps("a", 0.0, 0.0, 10000.0, 100.0, 0.0, 5.0, 1000.0);
    const AircraftState moved = geo::extrapolate(s, 1060.0);

    EXPECT_DOUBLE_EQ(moved.time_s, 1060.0);
    EXPECT_NEAR(geo::haversine_m(s.lat_deg, s.lon_deg, moved.lat_deg, moved.lon_deg), 6000.0, 0.5);
    EXPECT_GT(moved.lat_deg, s.lat_deg);
    EXPECT_NEAR(moved.lon_deg, s.lon_deg, 1e-12);
    EXPECT_NEAR(moved.alt_m, s.alt_m + 300.0, 1e-9);
}

TEST(Geo, ExtrapolateEastboundKeepsLatitude) {
    const AircraftState s = miniair::test::aircraft_mps("a", 0.0, 0.0, 30000.0, 230.0, 90.0);
    const AircraftState moved = geo::extrapolate(s, 30.0);
    EXPECT_NEAR(moved.lat_deg, s.lat_deg, 1e-12);
    EXPECT_GT(moved.lon_deg, s.lon_deg);
    EXPECT_NEAR(geo::haversine_m(s.lat_deg, s.lon_deg, moved.lat_deg, moved.lon_deg), 6900.0, 1.0);
}

TEST(Geo, ExtrapolateWrapsLongitude) {
    AircraftState s;
    s.lat_deg = 0.0;
    s.lon_deg = 179.99;
    s.ground_speed_mps = 250.0;
    s.track_deg = 90.0;
    const AircraftState moved = geo::extrapolate(s, 60.0);
    EXPECT_LT(moved.lon_deg, -179.0);
    EXPECT_GE(moved.lon_deg, -180.0);
}
