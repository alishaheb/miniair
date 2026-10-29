#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "miniair/geo.hpp"
#include "miniair/message_codec.hpp"

using miniair::AlertEvent;
using miniair::AlertKind;
using miniair::ParseResult;
using miniair::Severity;
using Json = nlohmann::json;

namespace {

Json valid_message() {
    return Json{{"icao24", "4ca7b5"}, {"callsign", "RYR12AB"}, {"t", 1730000000.0}, {"snapshot", 1730000005.0},
                {"lat", 51.47},       {"lon", -0.45},          {"alt_m", 10668.0},  {"gs_mps", 230.5},
                {"track_deg", 95.0},  {"vr_mps", -2.5},        {"on_ground", false}};
}

}  // namespace

TEST(MessageCodec, ParsesAValidMessage) {
    const ParseResult result = miniair::parse_state_report(valid_message().dump());
    ASSERT_TRUE(result.report.has_value()) << result.error;
    const miniair::AircraftState& s = result.report->state;
    EXPECT_EQ(s.icao24, "4ca7b5");
    EXPECT_EQ(s.callsign, "RYR12AB");
    EXPECT_DOUBLE_EQ(s.time_s, 1730000000.0);
    EXPECT_DOUBLE_EQ(result.report->snapshot_time_s, 1730000005.0);
    EXPECT_DOUBLE_EQ(s.lat_deg, 51.47);
    EXPECT_DOUBLE_EQ(s.lon_deg, -0.45);
    EXPECT_DOUBLE_EQ(s.alt_m, 10668.0);
    EXPECT_DOUBLE_EQ(s.ground_speed_mps, 230.5);
    EXPECT_DOUBLE_EQ(s.track_deg, 95.0);
    EXPECT_DOUBLE_EQ(s.vertical_rate_mps, -2.5);
    EXPECT_FALSE(s.on_ground);
}

TEST(MessageCodec, OptionalFieldsHaveDefaults) {
    Json message = valid_message();
    message.erase("callsign");
    message.erase("snapshot");
    message.erase("on_ground");
    const ParseResult result = miniair::parse_state_report(message.dump());
    ASSERT_TRUE(result.report.has_value()) << result.error;
    EXPECT_TRUE(result.report->state.callsign.empty());
    EXPECT_FALSE(result.report->state.on_ground);
    EXPECT_DOUBLE_EQ(result.report->snapshot_time_s, 1730000000.0);  // Falls back to t
}

TEST(MessageCodec, NullCallsignIsAllowed) {
    Json message = valid_message();
    message["callsign"] = nullptr;
    EXPECT_TRUE(miniair::parse_state_report(message.dump()).report.has_value());
}

TEST(MessageCodec, AcceptsIntegerNumbers) {
    Json message = valid_message();
    message["t"] = 1730000000;
    message["alt_m"] = 10668;
    EXPECT_TRUE(miniair::parse_state_report(message.dump()).report.has_value());
}

TEST(MessageCodec, RejectsMalformedJsonWithoutThrowing) {
    ParseResult result;
    EXPECT_NO_THROW(result = miniair::parse_state_report("{\"icao24\": \"abc\", "));
    EXPECT_FALSE(result.report.has_value());
    EXPECT_EQ(result.error, "not a JSON object");
}

TEST(MessageCodec, RejectsNonObjects) {
    EXPECT_FALSE(miniair::parse_state_report("[1, 2, 3]").report.has_value());
    EXPECT_FALSE(miniair::parse_state_report("42").report.has_value());
    EXPECT_FALSE(miniair::parse_state_report("").report.has_value());
}

TEST(MessageCodec, RejectsEmptyIcao24) {
    Json message = valid_message();
    message["icao24"] = "";
    const ParseResult result = miniair::parse_state_report(message.dump());
    EXPECT_FALSE(result.report.has_value());
    EXPECT_EQ(result.error, "missing or invalid field 'icao24'");
}

class MessageCodecRequiredField : public ::testing::TestWithParam<const char*> {};

TEST_P(MessageCodecRequiredField, MissingFieldIsRejected) {
    Json message = valid_message();
    message.erase(GetParam());
    const ParseResult result = miniair::parse_state_report(message.dump());
    EXPECT_FALSE(result.report.has_value());
    EXPECT_NE(result.error.find(GetParam()), std::string::npos) << result.error;
}

TEST_P(MessageCodecRequiredField, NullFieldIsRejected) {
    Json message = valid_message();
    message[GetParam()] = nullptr;
    EXPECT_FALSE(miniair::parse_state_report(message.dump()).report.has_value());
}

TEST_P(MessageCodecRequiredField, WrongTypeIsRejected) {
    Json message = valid_message();
    message[GetParam()] = Json::array({1, 2});
    EXPECT_FALSE(miniair::parse_state_report(message.dump()).report.has_value());
}

INSTANTIATE_TEST_SUITE_P(AllRequiredFields, MessageCodecRequiredField,
                         ::testing::Values("icao24", "t", "lat", "lon", "alt_m", "gs_mps", "track_deg", "vr_mps"));

TEST(MessageCodec, RejectsOutOfRangeValues) {
    const std::pair<const char*, double> bad_values[] = {
        {"lat", 90.5}, {"lat", -91.0}, {"lon", 180.5}, {"gs_mps", -1.0}, {"track_deg", -0.1}, {"track_deg", 360.5},
    };
    for (const auto& bad : bad_values) {
        Json message = valid_message();
        message[bad.first] = bad.second;
        EXPECT_FALSE(miniair::parse_state_report(message.dump()).report.has_value())
            << bad.first << " = " << bad.second;
    }
}

TEST(MessageCodec, AlertIsSerialisedInAviationUnits) {
    AlertEvent event;
    event.kind = AlertKind::Updated;
    event.cycle_time_s = 1730000100.0;
    event.conflict.icao24_a = "4ca7b5";
    event.conflict.icao24_b = "400a0b";
    event.conflict.callsign_a = "RYR12AB";
    event.conflict.callsign_b = "BAW45";
    event.conflict.severity = Severity::LossOfSeparation;
    event.conflict.time_to_conflict_s = 0.0;
    event.conflict.cpa_time_s = 42.04;
    event.conflict.cpa_horizontal_m = miniair::geo::nm_to_m(2.0);
    event.conflict.cpa_vertical_m = miniair::geo::ft_to_m(500.0);
    event.conflict.current_horizontal_m = miniair::geo::nm_to_m(4.5);
    event.conflict.current_vertical_m = miniair::geo::ft_to_m(800.0);

    const Json out = Json::parse(miniair::to_json(event));
    EXPECT_EQ(out["event"], "UPDATED");
    EXPECT_EQ(out["severity"], "LOSS_OF_SEPARATION");
    EXPECT_EQ(out["icao24_a"], "4ca7b5");
    EXPECT_EQ(out["callsign_b"], "BAW45");
    EXPECT_DOUBLE_EQ(out["cycle_time"].get<double>(), 1730000100.0);
    EXPECT_NEAR(out["cpa_time_s"].get<double>(), 42.0, 1e-9);
    EXPECT_NEAR(out["cpa_horizontal_nm"].get<double>(), 2.0, 1e-9);
    EXPECT_NEAR(out["cpa_vertical_ft"].get<double>(), 500.0, 1e-9);
    EXPECT_NEAR(out["current_horizontal_nm"].get<double>(), 4.5, 1e-9);
    EXPECT_NEAR(out["current_vertical_ft"].get<double>(), 800.0, 1e-9);
}

TEST(MessageCodec, RoundedValuesPrintCleanly) {
    AlertEvent event;
    event.conflict.cpa_horizontal_m = miniair::geo::nm_to_m(4.85);
    const std::string json = miniair::to_json(event);
    EXPECT_NE(json.find("\"cpa_horizontal_nm\":4.85,"), std::string::npos) << json;
}

TEST(MessageCodec, InvalidUtf8InCallsignDoesNotThrow) {
    AlertEvent event;
    event.conflict.callsign_a = std::string("BAD\xff\xfe", 5);
    std::string json;
    EXPECT_NO_THROW(json = miniair::to_json(event));
    EXPECT_TRUE(Json::accept(json));  // Output is still valid JSON
}
