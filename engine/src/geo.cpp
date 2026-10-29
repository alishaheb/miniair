#include "miniair/geo.hpp"

#include <cmath>

namespace miniair::geo {

namespace {

constexpr double kMinCosLat = 1.0e-6;  // Guard against division near the poles

// Wrap a longitude difference into [-180, 180) so pairs across the
// antimeridian are handled correctly.
double wrap_lon_delta_deg(double delta_deg) noexcept {
    double wrapped = std::fmod(delta_deg + 180.0, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped - 180.0;
}

}  // namespace

double haversine_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) noexcept {
    const double lat1 = deg_to_rad(lat1_deg);
    const double lat2 = deg_to_rad(lat2_deg);
    const double dlat = lat2 - lat1;
    const double dlon = deg_to_rad(wrap_lon_delta_deg(lon2_deg - lon1_deg));

    const double sin_dlat = std::sin(dlat / 2.0);
    const double sin_dlon = std::sin(dlon / 2.0);
    const double a = (sin_dlat * sin_dlat) + (std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon);
    return 2.0 * kEarthRadiusM * std::asin(std::sqrt(std::fmin(1.0, a)));
}

Vec2 relative_position_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) noexcept {
    const double mid_lat = deg_to_rad((lat1_deg + lat2_deg) / 2.0);
    const double dlat = deg_to_rad(lat2_deg - lat1_deg);
    const double dlon = deg_to_rad(wrap_lon_delta_deg(lon2_deg - lon1_deg));
    return Vec2{kEarthRadiusM * dlon * std::cos(mid_lat), kEarthRadiusM * dlat};
}

Vec2 velocity_mps(double ground_speed_mps, double track_deg) noexcept {
    const double track = deg_to_rad(track_deg);
    return Vec2{ground_speed_mps * std::sin(track), ground_speed_mps * std::cos(track)};
}

AircraftState extrapolate(const AircraftState& state, double to_time_s) noexcept {
    AircraftState moved = state;
    const double dt = to_time_s - state.time_s;
    const Vec2 velocity = velocity_mps(state.ground_speed_mps, state.track_deg);

    moved.lat_deg = state.lat_deg + rad_to_deg((velocity.y * dt) / kEarthRadiusM);
    const double cos_lat = std::cos(deg_to_rad(state.lat_deg));
    if (std::fabs(cos_lat) > kMinCosLat) {
        const double dlon = rad_to_deg((velocity.x * dt) / (kEarthRadiusM * cos_lat));
        moved.lon_deg = wrap_lon_delta_deg(state.lon_deg + dlon);
    }
    moved.alt_m = state.alt_m + (state.vertical_rate_mps * dt);
    moved.time_s = to_time_s;
    return moved;
}

}  // namespace miniair::geo
