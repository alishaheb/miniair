#ifndef MINIAIR_TRACK_STORE_HPP
#define MINIAIR_TRACK_STORE_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "miniair/types.hpp"

namespace miniair {

/// Holds the latest known state of every aircraft, keyed by ICAO address.
class TrackStore {
public:
    /// Store a report. Returns false (and keeps the old one) when the report
    /// is older than the state already held for that aircraft.
    bool update(const AircraftState& state);

    /// Drop tracks with no report for longer than max_age_s. Returns the
    /// number of tracks removed.
    std::size_t prune(double now_s, double max_age_s);

    /// Every track moved forward to the same time, ready for detection.
    [[nodiscard]] std::vector<AircraftState> picture_at(double time_s) const;

    [[nodiscard]] std::size_t size() const noexcept { return tracks_.size(); }

private:
    std::unordered_map<std::string, AircraftState> tracks_;
};

}  // namespace miniair

#endif  // MINIAIR_TRACK_STORE_HPP
