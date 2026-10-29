#include "miniair/alert_manager.hpp"

#include <algorithm>
#include <set>

namespace miniair {

const char* to_string(AlertKind kind) noexcept {
    switch (kind) {
        case AlertKind::New:
            return "NEW";
        case AlertKind::Updated:
            return "UPDATED";
        case AlertKind::Cleared:
            return "CLEARED";
        default:
            return "UNKNOWN";
    }
}

AlertManager::AlertManager(std::uint32_t clear_after_cycles) noexcept
    : clear_after_cycles_{std::max<std::uint32_t>(1U, clear_after_cycles)} {}

std::vector<AlertEvent> AlertManager::update(const std::vector<Conflict>& conflicts, double cycle_time_s) {
    std::vector<AlertEvent> events;
    std::set<PairKey> seen;

    for (const Conflict& conflict : conflicts) {
        PairKey key{conflict.icao24_a, conflict.icao24_b};
        const auto found = active_.find(key);
        if (found == active_.end()) {
            active_.emplace(key, ActiveAlert{conflict, 0U});
            events.push_back(AlertEvent{AlertKind::New, conflict, cycle_time_s});
        } else {
            if (found->second.last.severity != conflict.severity) {
                events.push_back(AlertEvent{AlertKind::Updated, conflict, cycle_time_s});
            }
            found->second.last = conflict;
            found->second.missed_cycles = 0U;
        }
        seen.insert(std::move(key));
    }

    for (auto it = active_.begin(); it != active_.end();) {
        if (seen.count(it->first) != 0U) {
            ++it;
            continue;
        }
        ++it->second.missed_cycles;
        if (it->second.missed_cycles >= clear_after_cycles_) {
            events.push_back(AlertEvent{AlertKind::Cleared, it->second.last, cycle_time_s});
            it = active_.erase(it);
        } else {
            ++it;
        }
    }
    return events;
}

}  // namespace miniair
