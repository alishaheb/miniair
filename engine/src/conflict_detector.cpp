#include "miniair/conflict_detector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "miniair/geo.hpp"

namespace miniair {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr double kEpsilon = 1.0e-9;

// Altitudes arrive in 25 ft steps, converted to metres. Without a small
// margin, rounding makes two aircraft exactly 1000 ft apart (a legal,
// everyday separation) look like 999.99 ft and raise a false alert.
constexpr double kAltitudeRoundingMarginM = geo::ft_to_m(1.0);

// An open time interval (start, end). Empty when start >= end.
struct Interval {
    double start;
    double end;
};

constexpr Interval kAllTime{-kInfinity, kInfinity};
constexpr Interval kNever{kInfinity, -kInfinity};

bool is_empty(const Interval& interval) noexcept { return interval.start >= interval.end; }

Interval intersect(const Interval& lhs, const Interval& rhs) noexcept {
    return Interval{std::max(lhs.start, rhs.start), std::min(lhs.end, rhs.end)};
}

double dot(const geo::Vec2& lhs, const geo::Vec2& rhs) noexcept {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y);
}

// Times when |r + v*t| < limit. Solves |v|^2 t^2 + 2(r.v) t + (|r|^2 - limit^2) < 0.
Interval horizontal_window(const geo::Vec2& r, const geo::Vec2& v, double limit) noexcept {
    const double a = dot(v, v);
    const double b = dot(r, v);
    const double c = dot(r, r) - (limit * limit);

    if (a < kEpsilon) {
        // Same velocity: the distance never changes.
        return (c < 0.0) ? kAllTime : kNever;
    }
    const double discriminant = (b * b) - (a * c);
    if (discriminant <= 0.0) {
        return kNever;
    }
    const double root = std::sqrt(discriminant);
    return Interval{(-b - root) / a, (-b + root) / a};
}

// Times when |dz + dvz*t| < limit.
Interval vertical_window(double dz, double dvz, double limit) noexcept {
    if (std::fabs(dvz) < kEpsilon) {
        return (std::fabs(dz) < limit) ? kAllTime : kNever;
    }
    const double t1 = (-limit - dz) / dvz;
    const double t2 = (limit - dz) / dvz;
    return Interval{std::min(t1, t2), std::max(t1, t2)};
}

}  // namespace

const char* to_string(Severity severity) noexcept {
    switch (severity) {
        case Severity::Predicted:
            return "PREDICTED";
        case Severity::LossOfSeparation:
            return "LOSS_OF_SEPARATION";
        default:
            return "UNKNOWN";
    }
}

ConflictDetector::ConflictDetector(const DetectorConfig& config) noexcept
    : horizontal_m_{geo::nm_to_m(config.horizontal_nm)},
      vertical_m_{geo::ft_to_m(config.vertical_ft) - kAltitudeRoundingMarginM},
      look_ahead_s_{config.look_ahead_s},
      min_altitude_m_{geo::ft_to_m(config.min_altitude_ft)} {}

bool ConflictDetector::is_eligible(const AircraftState& state) const noexcept {
    return (!state.on_ground) && (state.alt_m >= min_altitude_m_);
}

std::optional<Conflict> ConflictDetector::check_pair(const AircraftState& first,
                                                     const AircraftState& second) const {
    // Fixed order so that the same pair always produces the same key.
    const bool swap = second.icao24 < first.icao24;
    const AircraftState& a = swap ? second : first;
    const AircraftState& b = swap ? first : second;

    const geo::Vec2 r = geo::relative_position_m(a.lat_deg, a.lon_deg, b.lat_deg, b.lon_deg);
    const geo::Vec2 va = geo::velocity_mps(a.ground_speed_mps, a.track_deg);
    const geo::Vec2 vb = geo::velocity_mps(b.ground_speed_mps, b.track_deg);
    const geo::Vec2 v{vb.x - va.x, vb.y - va.y};
    const double dz = b.alt_m - a.alt_m;
    const double dvz = b.vertical_rate_mps - a.vertical_rate_mps;

    // Both minima must be infringed at the same time, inside the look-ahead.
    const Interval window = intersect(
        intersect(horizontal_window(r, v, horizontal_m_), vertical_window(dz, dvz, vertical_m_)),
        Interval{0.0, look_ahead_s_});
    if (is_empty(window)) {
        return std::nullopt;
    }

    // Closest horizontal approach, kept inside the conflict window.
    const double speed_sq = dot(v, v);
    double cpa_time = window.start;
    if (speed_sq >= kEpsilon) {
        cpa_time = std::clamp(-dot(r, v) / speed_sq, window.start, window.end);
    }
    const geo::Vec2 r_cpa{r.x + (v.x * cpa_time), r.y + (v.y * cpa_time)};

    const double current_horizontal = std::sqrt(dot(r, r));
    const double current_vertical = std::fabs(dz);
    const bool separated_now = (current_horizontal >= horizontal_m_) || (current_vertical >= vertical_m_);

    Conflict conflict;
    conflict.icao24_a = a.icao24;
    conflict.icao24_b = b.icao24;
    conflict.callsign_a = a.callsign;
    conflict.callsign_b = b.callsign;
    conflict.severity = separated_now ? Severity::Predicted : Severity::LossOfSeparation;
    conflict.time_to_conflict_s = window.start;
    conflict.cpa_time_s = cpa_time;
    conflict.cpa_horizontal_m = std::sqrt(dot(r_cpa, r_cpa));
    conflict.cpa_vertical_m = std::fabs(dz + (dvz * cpa_time));
    conflict.current_horizontal_m = current_horizontal;
    conflict.current_vertical_m = current_vertical;
    return conflict;
}

std::vector<Conflict> ConflictDetector::detect(const std::vector<AircraftState>& picture) const {
    std::vector<const AircraftState*> eligible;
    eligible.reserve(picture.size());
    double max_speed = 0.0;
    for (const AircraftState& state : picture) {
        if (is_eligible(state)) {
            eligible.push_back(&state);
            max_speed = std::max(max_speed, state.ground_speed_mps);
        }
    }

    // Sweep by latitude. Two aircraft can close by at most 2 * max_speed per
    // second, so a pair whose north-south gap is beyond "reach" cannot come
    // within the horizontal minimum during the look-ahead.
    std::sort(eligible.begin(), eligible.end(),
              [](const AircraftState* lhs, const AircraftState* rhs) { return lhs->lat_deg < rhs->lat_deg; });
    const double reach_m = horizontal_m_ + (2.0 * max_speed * look_ahead_s_);
    const double reach_deg = geo::rad_to_deg(reach_m / geo::kEarthRadiusM);

    std::vector<Conflict> conflicts;
    for (std::size_t i = 0U; i < eligible.size(); ++i) {
        const AircraftState& a = *eligible[i];
        for (std::size_t j = i + 1U; j < eligible.size(); ++j) {
            const AircraftState& b = *eligible[j];
            if ((b.lat_deg - a.lat_deg) > reach_deg) {
                break;  // Every later aircraft is further north still
            }
            // Cheap vertical filter before the trigonometry.
            const double dz = std::fabs(b.alt_m - a.alt_m);
            const double max_vertical_closure = std::fabs(b.vertical_rate_mps - a.vertical_rate_mps) * look_ahead_s_;
            if ((dz - max_vertical_closure) >= vertical_m_) {
                continue;
            }
            std::optional<Conflict> conflict = check_pair(a, b);
            if (conflict.has_value()) {
                conflicts.push_back(std::move(conflict.value()));
            }
        }
    }

    std::sort(conflicts.begin(), conflicts.end(), [](const Conflict& lhs, const Conflict& rhs) {
        return (lhs.icao24_a != rhs.icao24_a) ? (lhs.icao24_a < rhs.icao24_a) : (lhs.icao24_b < rhs.icao24_b);
    });
    return conflicts;
}

}  // namespace miniair
