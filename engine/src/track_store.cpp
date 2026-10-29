#include "miniair/track_store.hpp"

#include "miniair/geo.hpp"

namespace miniair {

bool TrackStore::update(const AircraftState& state) {
    const auto found = tracks_.find(state.icao24);
    if (found == tracks_.end()) {
        tracks_.emplace(state.icao24, state);
        return true;
    }
    if (state.time_s < found->second.time_s) {
        return false;  // Out-of-order report: keep the newer one
    }
    found->second = state;
    return true;
}

std::size_t TrackStore::prune(double now_s, double max_age_s) {
    std::size_t removed = 0U;
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        if ((now_s - it->second.time_s) > max_age_s) {
            it = tracks_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::vector<AircraftState> TrackStore::picture_at(double time_s) const {
    std::vector<AircraftState> picture;
    picture.reserve(tracks_.size());
    for (const auto& entry : tracks_) {
        picture.push_back(geo::extrapolate(entry.second, time_s));
    }
    return picture;
}

}  // namespace miniair
