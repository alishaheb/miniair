#ifndef MINIAIR_GEO_HPP
#define MINIAIR_GEO_HPP

#include "miniair/types.hpp"

namespace miniair::geo {

constexpr double kEarthRadiusM = 6371000.0;  // Mean Earth radius
constexpr double kMetresPerNauticalMile = 1852.0;
constexpr double kMetresPerFoot = 0.3048;
constexpr double kPi = 3.14159265358979323846;

constexpr double deg_to_rad(double deg) noexcept { return deg * kPi / 180.0; }
constexpr double rad_to_deg(double rad) noexcept { return rad * 180.0 / kPi; }
constexpr double nm_to_m(double nm) noexcept { return nm * kMetresPerNauticalMile; }
constexpr double m_to_nm(double m) noexcept { return m / kMetresPerNauticalMile; }
constexpr double ft_to_m(double ft) noexcept { return ft * kMetresPerFoot; }
constexpr double m_to_ft(double m) noexcept { return m / kMetresPerFoot; }

/// A point or vector on a local flat plane: x points east, y points north.
struct Vec2 {
    double x{0.0};
    double y{0.0};
};

/// Great-circle distance in metres (haversine formula).
double haversine_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) noexcept;

/// Position of point 2 relative to point 1 on a flat plane tangent near the
/// two points. Accurate to well under 1% for the short ranges used here.
Vec2 relative_position_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) noexcept;

/// Horizontal velocity vector from ground speed and true track.
Vec2 velocity_mps(double ground_speed_mps, double track_deg) noexcept;

/// Move an aircraft along its current velocity to the given time.
AircraftState extrapolate(const AircraftState& state, double to_time_s) noexcept;

}  // namespace miniair::geo

#endif  // MINIAIR_GEO_HPP
