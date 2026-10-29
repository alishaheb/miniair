import math

import pytest

from miniair_ingest import synthetic
from miniair_ingest.schema import missing_fields

START = 1700000000.0


def haversine_m(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * synthetic.EARTH_RADIUS_M * math.asin(math.sqrt(a))


def by_callsign(messages):
    return {m["callsign"]: m for m in messages}


def test_scenario_has_one_snapshot_per_step_including_both_ends():
    snaps = list(synthetic.snapshots(START, duration_s=300.0, step_s=5.0))
    assert len(snaps) == 61
    assert snaps[0][0] == START
    assert snaps[-1][0] == START + 300.0
    assert all(len(messages) == len(synthetic.FLIGHTS) for _, messages in snaps)


def test_messages_are_complete_and_stamped_with_the_snapshot_time():
    for snapshot_time, messages in synthetic.snapshots(START, duration_s=10.0):
        for message in messages:
            assert missing_fields(message) == []
            assert message["t"] == snapshot_time
            assert message["snapshot"] == snapshot_time
            assert message["source"] == "synthetic"


def test_positions_move_at_the_stated_ground_speed():
    snaps = list(synthetic.snapshots(START, duration_s=10.0, step_s=10.0))
    before, after = by_callsign(snaps[0][1]), by_callsign(snaps[1][1])
    for flight in synthetic.FLIGHTS:
        a, b = before[flight.callsign], after[flight.callsign]
        moved = haversine_m(a["lat"], a["lon"], b["lat"], b["lon"])
        assert moved == pytest.approx(flight.speed_mps * 10.0, rel=1e-3), flight.callsign


def test_crossing_pair_meets_at_the_crossing_point():
    # Both start 50 km out at 230 m/s, so they meet after 217.4 s.
    meet = 50000.0 / 230.0
    snapshot = next(iter(synthetic.snapshots(START + 0.0, duration_s=0.0)))  # Start geometry
    start = by_callsign(snapshot[1])
    assert haversine_m(
        start["MNA101"]["lat"], start["MNA101"]["lon"], start["MNA102"]["lat"], start["MNA102"]["lon"]
    ) == pytest.approx(50000.0 * math.sqrt(2.0), rel=0.01)

    flight_a, flight_b = synthetic.FLIGHTS[0], synthetic.FLIGHTS[1]
    a, b = flight_a.state_at(meet, START + meet), flight_b.state_at(meet, START + meet)
    assert haversine_m(a["lat"], a["lon"], b["lat"], b["lon"]) < 200.0
    assert a["alt_m"] == b["alt_m"]


def test_separated_pair_stays_two_thousand_feet_apart():
    for _, messages in synthetic.snapshots(START):
        flights = by_callsign(messages)
        gap_ft = (flights["MNA202"]["alt_m"] - flights["MNA201"]["alt_m"]) / synthetic.FEET_TO_M
        assert gap_ft == pytest.approx(2000.0)


def test_climber_passes_through_the_level_aircraft():
    snaps = {t - START: by_callsign(m) for t, m in synthetic.snapshots(START)}
    level = snaps[0.0]["MNA302"]["alt_m"]
    assert snaps[0.0]["MNA301"]["alt_m"] < level < snaps[300.0]["MNA301"]["alt_m"]


def test_realtime_mode_sleeps_between_snapshots():
    sleeps = []
    list(synthetic.snapshots(START, duration_s=10.0, step_s=5.0, realtime=True, sleep=sleeps.append))
    assert sleeps == [5.0, 5.0, 5.0]


def test_loop_mode_keeps_time_moving_forward():
    stream = synthetic.snapshots(START, duration_s=10.0, step_s=5.0, loop=True)
    times = [next(stream)[0] for _ in range(7)]
    assert times == [START + 5.0 * i for i in range(7)]
