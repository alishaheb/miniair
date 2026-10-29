#ifndef MINIAIR_TYPES_HPP
#define MINIAIR_TYPES_HPP

#include <cstdint>
#include <string>

namespace miniair {

/// One surveillance report for one aircraft. All values are SI units.
struct AircraftState {
    std::string icao24;            ///< 24-bit ICAO address, lower-case hex
    std::string callsign;          ///< May be empty
    double time_s{0.0};            ///< Unix time of the position fix
    double lat_deg{0.0};           ///< WGS-84 latitude
    double lon_deg{0.0};           ///< WGS-84 longitude
    double alt_m{0.0};             ///< Barometric (pressure) altitude
    double ground_speed_mps{0.0};
    double track_deg{0.0};         ///< True track, clockwise from north
    double vertical_rate_mps{0.0}; ///< Positive when climbing
    bool on_ground{false};
};

/// Detector settings in aviation units, as a controller would state them.
struct DetectorConfig {
    double horizontal_nm{5.0};      ///< En-route radar separation minimum
    double vertical_ft{1000.0};     ///< RVSM vertical separation minimum
    double look_ahead_s{120.0};     ///< How far ahead to predict
    double min_altitude_ft{1000.0}; ///< Ignore traffic below this (runway area)
};

enum class Severity : std::uint8_t {
    Predicted,        ///< Minima will be infringed within the look-ahead time
    LossOfSeparation  ///< Minima are infringed now
};

/// A conflict between two aircraft. icao24_a is always less than icao24_b.
struct Conflict {
    std::string icao24_a;
    std::string icao24_b;
    std::string callsign_a;
    std::string callsign_b;
    Severity severity{Severity::Predicted};
    double time_to_conflict_s{0.0};  ///< 0 when separation is already lost
    double cpa_time_s{0.0};          ///< Time of closest horizontal approach
    double cpa_horizontal_m{0.0};    ///< Horizontal distance at CPA
    double cpa_vertical_m{0.0};      ///< Vertical distance at CPA
    double current_horizontal_m{0.0};
    double current_vertical_m{0.0};
};

const char* to_string(Severity severity) noexcept;

}  // namespace miniair

#endif  // MINIAIR_TYPES_HPP
