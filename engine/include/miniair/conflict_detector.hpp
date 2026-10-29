#ifndef MINIAIR_CONFLICT_DETECTOR_HPP
#define MINIAIR_CONFLICT_DETECTOR_HPP

#include <optional>
#include <vector>

#include "miniair/types.hpp"

namespace miniair {

/// Finds pairs of aircraft that have lost, or will lose, separation.
///
/// Each aircraft is assumed to fly straight at constant speed and constant
/// vertical rate for the look-ahead time. A pair is in conflict when there is
/// a time in [0, look-ahead] at which the pair is closer than the horizontal
/// minimum AND closer than the vertical minimum at the same moment.
class ConflictDetector {
public:
    explicit ConflictDetector(const DetectorConfig& config) noexcept;

    /// True when the aircraft should take part in detection: airborne and
    /// above the minimum altitude.
    [[nodiscard]] bool is_eligible(const AircraftState& state) const noexcept;

    /// Check one pair. Both states must refer to the same time.
    [[nodiscard]] std::optional<Conflict> check_pair(const AircraftState& first,
                                                     const AircraftState& second) const;

    /// Check every eligible pair in a traffic picture. All states must refer
    /// to the same time. Results are sorted by (icao24_a, icao24_b).
    [[nodiscard]] std::vector<Conflict> detect(const std::vector<AircraftState>& picture) const;

private:
    double horizontal_m_;
    double vertical_m_;
    double look_ahead_s_;
    double min_altitude_m_;
};

}  // namespace miniair

#endif  // MINIAIR_CONFLICT_DETECTOR_HPP
