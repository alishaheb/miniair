#ifndef MINIAIR_TEST_HELPERS_HPP
#define MINIAIR_TEST_HELPERS_HPP

#include <cmath>
#include <string>

#include "miniair/geo.hpp"
#include "miniair/types.hpp"

namespace miniair::test {

// Reference point for test geometry: over the English Midlands.
constexpr double kRefLatDeg = 52.0;
constexpr double kRefLonDeg = -1.0;

/// Build an aircraft placed east_m / north_m from the reference point.
/// Speeds in knots and feet per minute keep the tests readable.
inline AircraftState aircraft(const std::string& icao24, double east_m, double north_m, double alt_ft,
                              double speed_kt, double track_deg, double vrate_fpm = 0.0, double time_s = 0.0) {
    constexpr double kMpsPerKnot = 1852.0 / 3600.0;
    constexpr double kMpsPerFpm = 0.3048 / 60.0;
    AircraftState s;
    s.icao24 = icao24;
    s.callsign = "T" + icao24;
    s.time_s = time_s;
    s.lat_deg = kRefLatDeg + geo::rad_to_deg(north_m / geo::kEarthRadiusM);
    s.lon_deg = kRefLonDeg + geo::rad_to_deg(east_m / (geo::kEarthRadiusM * std::cos(geo::deg_to_rad(kRefLatDeg))));
    s.alt_m = geo::ft_to_m(alt_ft);
    s.ground_speed_mps = speed_kt * kMpsPerKnot;
    s.track_deg = track_deg;
    s.vertical_rate_mps = vrate_fpm * kMpsPerFpm;
    return s;
}

/// Same, but with the speed given directly in metres per second.
inline AircraftState aircraft_mps(const std::string& icao24, double east_m, double north_m, double alt_ft,
                                  double speed_mps, double track_deg, double vrate_mps = 0.0, double time_s = 0.0) {
    AircraftState s = aircraft(icao24, east_m, north_m, alt_ft, 0.0, track_deg, 0.0, time_s);
    s.ground_speed_mps = speed_mps;
    s.vertical_rate_mps = vrate_mps;
    return s;
}

}  // namespace miniair::test

#endif  // MINIAIR_TEST_HELPERS_HPP
