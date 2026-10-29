#include "miniair/engine.hpp"

#include <cmath>

#include "miniair/geo.hpp"

namespace miniair {

Engine::Engine(const EngineConfig& config)
    : detector_{config.detector},
      alerts_{config.clear_after_cycles},
      max_track_age_s_{config.max_track_age_s},
      level_rate_mps_{geo::ft_to_m(config.level_flight_fpm) / 60.0} {}

std::vector<AlertEvent> Engine::on_report(const StateReport& report) {
    std::vector<AlertEvent> events;
    const bool newer_snapshot = (!seen_any_) || (report.snapshot_time_s > latest_snapshot_s_);
    if (newer_snapshot && has_pending_) {
        events = run_cycle(latest_snapshot_s_);
    }
    // Remove vertical-rate noise once, here, so extrapolation and prediction
    // both see the same cleaned data.
    AircraftState state = report.state;
    if (std::fabs(state.vertical_rate_mps) < level_rate_mps_) {
        state.vertical_rate_mps = 0.0;
    }
    // A late report from an older snapshot still updates its track, but does
    // not move the clock backwards.
    (void)tracks_.update(state);
    if (newer_snapshot) {
        latest_snapshot_s_ = report.snapshot_time_s;
        seen_any_ = true;
    }
    has_pending_ = true;
    return events;
}

std::vector<AlertEvent> Engine::flush() {
    if (!has_pending_) {
        return {};
    }
    return run_cycle(latest_snapshot_s_);
}

std::vector<AlertEvent> Engine::run_cycle(double time_s) {
    has_pending_ = false;
    (void)tracks_.prune(time_s, max_track_age_s_);
    const std::vector<AircraftState> picture = tracks_.picture_at(time_s);
    const std::vector<Conflict> conflicts = detector_.detect(picture);
    std::vector<AlertEvent> events = alerts_.update(conflicts, time_s);

    last_cycle_ = CycleSummary{time_s, picture.size(), conflicts.size(), events.size()};
    ++cycles_run_;
    return events;
}

}  // namespace miniair
