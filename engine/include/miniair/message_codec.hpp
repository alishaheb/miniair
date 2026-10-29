#ifndef MINIAIR_MESSAGE_CODEC_HPP
#define MINIAIR_MESSAGE_CODEC_HPP

#include <optional>
#include <string>
#include <string_view>

#include "miniair/alert_manager.hpp"
#include "miniair/types.hpp"

namespace miniair {

/// One message from the flights.states topic.
struct StateReport {
    AircraftState state;
    double snapshot_time_s{0.0};  ///< Time of the surveillance picture this came from
};

/// Either a report, or the reason the message was rejected.
struct ParseResult {
    std::optional<StateReport> report;
    std::string error;
};

/// Parse and validate one JSON state message. Never throws: bad input gives
/// a ParseResult with an error message instead.
[[nodiscard]] ParseResult parse_state_report(std::string_view text);

/// Serialise an alert event as one line of JSON, in aviation units
/// (nautical miles and feet) for people reading it.
[[nodiscard]] std::string to_json(const AlertEvent& event);

}  // namespace miniair

#endif  // MINIAIR_MESSAGE_CODEC_HPP
