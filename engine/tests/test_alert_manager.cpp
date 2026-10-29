#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "miniair/alert_manager.hpp"

using miniair::AlertEvent;
using miniair::AlertKind;
using miniair::AlertManager;
using miniair::Conflict;
using miniair::Severity;

namespace {

Conflict conflict(const std::string& a, const std::string& b, Severity severity = Severity::Predicted) {
    Conflict c;
    c.icao24_a = a;
    c.icao24_b = b;
    c.severity = severity;
    return c;
}

}  // namespace

TEST(AlertManager, FirstConflictRaisesNew) {
    AlertManager manager{2U};
    const std::vector<AlertEvent> events = manager.update({conflict("a", "b")}, 10.0);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, AlertKind::New);
    EXPECT_EQ(events[0].conflict.icao24_a, "a");
    EXPECT_DOUBLE_EQ(events[0].cycle_time_s, 10.0);
    EXPECT_EQ(manager.active_count(), 1U);
}

TEST(AlertManager, UnchangedConflictRaisesNothing) {
    AlertManager manager{2U};
    (void)manager.update({conflict("a", "b")}, 10.0);
    EXPECT_TRUE(manager.update({conflict("a", "b")}, 15.0).empty());
    EXPECT_TRUE(manager.update({conflict("a", "b")}, 20.0).empty());
}

TEST(AlertManager, SeverityChangeRaisesUpdated) {
    AlertManager manager{2U};
    (void)manager.update({conflict("a", "b", Severity::Predicted)}, 10.0);
    const std::vector<AlertEvent> events = manager.update({conflict("a", "b", Severity::LossOfSeparation)}, 15.0);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, AlertKind::Updated);
    EXPECT_EQ(events[0].conflict.severity, Severity::LossOfSeparation);
}

TEST(AlertManager, ClearedOnlyAfterConfiguredMissedCycles) {
    AlertManager manager{2U};
    (void)manager.update({conflict("a", "b")}, 10.0);

    EXPECT_TRUE(manager.update({}, 15.0).empty());  // First miss: hold the alert
    const std::vector<AlertEvent> events = manager.update({}, 20.0);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, AlertKind::Cleared);
    EXPECT_EQ(events[0].conflict.icao24_b, "b");  // Carries the last known details
    EXPECT_EQ(manager.active_count(), 0U);
}

TEST(AlertManager, ReappearingBeforeClearDoesNotRaiseNewAgain) {
    AlertManager manager{2U};
    (void)manager.update({conflict("a", "b")}, 10.0);
    (void)manager.update({}, 15.0);
    EXPECT_TRUE(manager.update({conflict("a", "b")}, 20.0).empty());
    EXPECT_TRUE(manager.update({}, 25.0).empty());  // Miss counter was reset
}

TEST(AlertManager, ZeroClearAfterIsTreatedAsOne) {
    AlertManager manager{0U};
    (void)manager.update({conflict("a", "b")}, 10.0);
    const std::vector<AlertEvent> events = manager.update({}, 15.0);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, AlertKind::Cleared);
}

TEST(AlertManager, PairsAreTrackedIndependently) {
    AlertManager manager{1U};
    (void)manager.update({conflict("a", "b"), conflict("c", "d")}, 10.0);
    const std::vector<AlertEvent> events = manager.update({conflict("c", "d"), conflict("e", "f")}, 15.0);

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].kind, AlertKind::New);
    EXPECT_EQ(events[0].conflict.icao24_a, "e");
    EXPECT_EQ(events[1].kind, AlertKind::Cleared);
    EXPECT_EQ(events[1].conflict.icao24_a, "a");
    EXPECT_EQ(manager.active_count(), 2U);
}

TEST(AlertKindNames, AreStable) {
    EXPECT_STREQ(miniair::to_string(AlertKind::New), "NEW");
    EXPECT_STREQ(miniair::to_string(AlertKind::Updated), "UPDATED");
    EXPECT_STREQ(miniair::to_string(AlertKind::Cleared), "CLEARED");
}
