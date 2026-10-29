#include "miniair/message_codec.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

#include "miniair/geo.hpp"

namespace miniair {

namespace {

using Json = nlohmann::json;

std::optional<double> read_number(const Json& object, const char* key) {
    const auto it = object.find(key);
    if ((it == object.end()) || (!it->is_number())) {
        return std::nullopt;
    }
    const double value = it->get<double>();
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

ParseResult reject(const std::string& reason) { return ParseResult{std::nullopt, reason}; }

ParseResult missing(const char* key) { return reject(std::string("missing or invalid field '") + key + "'"); }

// Round to a number of decimal places. Dividing by a whole power of ten (not
// multiplying by 0.01) gives the closest double, so 4.85 prints as 4.85.
double round_to(double value, int decimals) {
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

}  // namespace

ParseResult parse_state_report(std::string_view text) {
    // allow_exceptions = false: a parse error gives a "discarded" value.
    const Json object = Json::parse(text.begin(), text.end(), nullptr, false);
    if (object.is_discarded() || (!object.is_object())) {
        return reject("not a JSON object");
    }

    StateReport report;
    AircraftState& s = report.state;

    const auto icao = object.find("icao24");
    if ((icao == object.end()) || (!icao->is_string()) || icao->get_ref<const std::string&>().empty()) {
        return missing("icao24");
    }
    s.icao24 = icao->get<std::string>();

    const auto callsign = object.find("callsign");
    if ((callsign != object.end()) && callsign->is_string()) {
        s.callsign = callsign->get<std::string>();
    }

    const std::optional<double> time = read_number(object, "t");
    const std::optional<double> lat = read_number(object, "lat");
    const std::optional<double> lon = read_number(object, "lon");
    const std::optional<double> alt = read_number(object, "alt_m");
    const std::optional<double> speed = read_number(object, "gs_mps");
    const std::optional<double> track = read_number(object, "track_deg");
    const std::optional<double> vrate = read_number(object, "vr_mps");

    if (!time.has_value()) {
        return missing("t");
    }
    if ((!lat.has_value()) || (std::fabs(lat.value()) > 90.0)) {
        return missing("lat");
    }
    if ((!lon.has_value()) || (std::fabs(lon.value()) > 180.0)) {
        return missing("lon");
    }
    if (!alt.has_value()) {
        return missing("alt_m");
    }
    if ((!speed.has_value()) || (speed.value() < 0.0)) {
        return missing("gs_mps");
    }
    if ((!track.has_value()) || (track.value() < 0.0) || (track.value() > 360.0)) {
        return missing("track_deg");
    }
    if (!vrate.has_value()) {
        return missing("vr_mps");
    }

    s.time_s = time.value();
    s.lat_deg = lat.value();
    s.lon_deg = lon.value();
    s.alt_m = alt.value();
    s.ground_speed_mps = speed.value();
    s.track_deg = track.value();
    s.vertical_rate_mps = vrate.value();

    const auto on_ground = object.find("on_ground");
    s.on_ground = (on_ground != object.end()) && on_ground->is_boolean() && on_ground->get<bool>();

    // Older producers may not send a snapshot time; fall back to the fix time.
    report.snapshot_time_s = read_number(object, "snapshot").value_or(s.time_s);

    return ParseResult{report, std::string{}};
}

std::string to_json(const AlertEvent& event) {
    const Conflict& c = event.conflict;
    Json object;
    object["event"] = to_string(event.kind);
    object["severity"] = to_string(c.severity);
    object["cycle_time"] = event.cycle_time_s;
    object["icao24_a"] = c.icao24_a;
    object["callsign_a"] = c.callsign_a;
    object["icao24_b"] = c.icao24_b;
    object["callsign_b"] = c.callsign_b;
    object["time_to_conflict_s"] = round_to(c.time_to_conflict_s, 1);
    object["cpa_time_s"] = round_to(c.cpa_time_s, 1);
    object["cpa_horizontal_nm"] = round_to(geo::m_to_nm(c.cpa_horizontal_m), 2);
    object["cpa_vertical_ft"] = round_to(geo::m_to_ft(c.cpa_vertical_m), 0);
    object["current_horizontal_nm"] = round_to(geo::m_to_nm(c.current_horizontal_m), 2);
    object["current_vertical_ft"] = round_to(geo::m_to_ft(c.current_vertical_m), 0);
    // "replace" stops dump() throwing if a callsign holds invalid UTF-8.
    return object.dump(-1, ' ', false, Json::error_handler_t::replace);
}

}  // namespace miniair
