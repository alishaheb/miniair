#ifndef MINIAIR_ALERT_MANAGER_HPP
#define MINIAIR_ALERT_MANAGER_HPP

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "miniair/types.hpp"

namespace miniair {

enum class AlertKind : std::uint8_t {
    New,      ///< First cycle the pair is in conflict
    Updated,  ///< Severity changed (for example predicted -> loss of separation)
    Cleared   ///< Pair has been conflict-free for the configured number of cycles
};

const char* to_string(AlertKind kind) noexcept;

struct AlertEvent {
    AlertKind kind{AlertKind::New};
    Conflict conflict;
    double cycle_time_s{0.0};
};

/// Turns the conflicts found in each cycle into change events, so a
/// controller sees one alert per conflict instead of one per cycle.
class AlertManager {
public:
    /// clear_after_cycles: how many conflict-free cycles in a row before an
    /// alert is cleared. A value above 1 stops alerts flickering on noisy data.
    explicit AlertManager(std::uint32_t clear_after_cycles) noexcept;

    std::vector<AlertEvent> update(const std::vector<Conflict>& conflicts, double cycle_time_s);

    [[nodiscard]] std::size_t active_count() const noexcept { return active_.size(); }

private:
    using PairKey = std::pair<std::string, std::string>;

    struct ActiveAlert {
        Conflict last;
        std::uint32_t missed_cycles{0U};
    };

    std::uint32_t clear_after_cycles_;
    std::map<PairKey, ActiveAlert> active_;
};

}  // namespace miniair

#endif  // MINIAIR_ALERT_MANAGER_HPP
