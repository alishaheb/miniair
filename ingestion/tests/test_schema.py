from miniair_ingest.schema import REQUIRED_FIELDS, from_opensky, missing_fields


def opensky_row(**overrides):
    """A state vector in OpenSky's array layout (17 fields)."""
    row = [
        "4CA7B5",  # icao24
        "RYR12AB ",  # callsign, padded as OpenSky sends it
        "Ireland",  # origin_country
        1730000005,  # time_position
        1730000008,  # last_contact
        -0.45,  # longitude
        51.47,  # latitude
        10668.0,  # baro_altitude
        False,  # on_ground
        230.5,  # velocity
        95.0,  # true_track
        -2.5,  # vertical_rate
        None,  # sensors
        10900.0,  # geo_altitude
        "1234",  # squawk
        False,  # spi
        0,  # position_source
    ]
    names = {
        "icao24": 0,
        "callsign": 1,
        "time_position": 3,
        "last_contact": 4,
        "lon": 5,
        "lat": 6,
        "baro_altitude": 7,
        "on_ground": 8,
        "velocity": 9,
        "track": 10,
        "vertical_rate": 11,
    }
    for key, value in overrides.items():
        row[names[key]] = value
    return row


def test_converts_a_complete_row():
    message = from_opensky(opensky_row(), snapshot_time=1730000010.0)
    assert message == {
        "icao24": "4ca7b5",
        "callsign": "RYR12AB",
        "t": 1730000005.0,
        "snapshot": 1730000010.0,
        "lat": 51.47,
        "lon": -0.45,
        "alt_m": 10668.0,
        "gs_mps": 230.5,
        "track_deg": 95.0,
        "vr_mps": -2.5,
        "on_ground": False,
        "source": "opensky",
    }
    assert missing_fields(message) == []


def test_uses_barometric_not_geometric_altitude():
    message = from_opensky(opensky_row(baro_altitude=9000.0), 0.0)
    assert message["alt_m"] == 9000.0


def test_rows_missing_detection_fields_are_dropped():
    for field in ("lat", "lon", "baro_altitude", "velocity", "track"):
        assert from_opensky(opensky_row(**{field: None}), 0.0) is None, field
    assert from_opensky(opensky_row(icao24=""), 0.0) is None


def test_unknown_vertical_rate_means_level_flight():
    assert from_opensky(opensky_row(vertical_rate=None), 0.0)["vr_mps"] == 0.0


def test_missing_position_time_falls_back_to_last_contact():
    assert from_opensky(opensky_row(time_position=None), 0.0)["t"] == 1730000008.0
    assert from_opensky(opensky_row(time_position=None, last_contact=None), 0.0) is None


def test_null_callsign_becomes_empty():
    assert from_opensky(opensky_row(callsign=None), 0.0)["callsign"] == ""


def test_short_rows_are_dropped():
    assert from_opensky(["4ca7b5", "RYR"], 0.0) is None


def test_track_is_normalised_to_compass_range():
    assert from_opensky(opensky_row(track=360.0), 0.0)["track_deg"] == 0.0


def test_missing_fields_lists_null_and_absent_fields():
    message = {name: 1.0 for name in REQUIRED_FIELDS}
    message["lat"] = None
    del message["vr_mps"]
    assert missing_fields(message) == ["lat", "vr_mps"]
