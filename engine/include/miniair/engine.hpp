#ifndef MINIAIR_ENGINE_HPP
#define MINIAIR_ENGINE_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "miniair/alert_manager.hpp"
#include "miniair/conflict_detector.hpp"
#include "miniair/message_codec.hpp"
#include "miniair/track_store.hpp"

namespace miniair {

struct EngineConfig {
    DetectorConfig detector;
    double max_track_age_s{60.0};       ///< Drop aircraft not heard from for this long
    std::uint32_t clear_after_cycles{2U};
    /// Vertical rates below this are treated as level flight. ADS-B reports
    /// vertical rate in 64 ft/min steps, and level aircraft often show one or
    /// two steps of noise. Left in, that noise moves altitudes during
    /// extrapolation and prediction, and raises false conflicts between
    /// aircraft exactly 1000 ft apart.
    double level_flight_fpm{150.0};
};

/// Summary of one detection cycle, for logging.
struct CycleSummary {
    double time_s{0.0};
    std::size_t tracks{0U};
    std::size_t conflicts{0U};
    std::size_t events{0U};
};

/// Ties the parts together. Reports arrive grouped by snapshot (one OpenSky
/// poll, or one radar scan). When a report from a newer snapshot arrives, the
/// previous snapshot is complete, so one detection cycle runs for it.
class Engine {
public:
    explicit Engine(const EngineConfig& config);

    /// Feed one report. Returns the alert events from any cycle it triggered.
    std::vector<AlertEvent> on_report(const StateReport& report);

    /// Run a cycle for the snapshot still waiting, if there is one. Call this
    /// at the end of input, or when no messages have arrived for a while.
    std::vector<AlertEvent> flush();

    [[nodiscard]] const CycleSummary& last_cycle() const noexcept { return last_cycle_; }
    [[nodiscard]] std::uint64_t cycles_run() const noexcept { return cycles_run_; }
    [[nodiscard]] std::size_t active_alerts() const noexcept { return alerts_.active_count(); }

private:
    std::vector<AlertEvent> run_cycle(double time_s);

    ConflictDetector detector_;
    TrackStore tracks_;
    AlertManager alerts_;
    double max_track_age_s_;
    double level_rate_mps_;
    bool seen_any_{false};
    bool has_pending_{false};  ///< Reports received since the last cycle
    double latest_snapshot_s_{0.0};
    CycleSummary last_cycle_;
    std::uint64_t cycles_run_{0U};
};

}  // namespace miniair

#endif  // MINIAIR_ENGINE_HPP
