"""The flights.states message format, and conversion from OpenSky data.

One message describes one aircraft at one moment, in SI units:

    {"icao24": "4ca7b5", "callsign": "RYR12AB", "t": 1730000000.0,
     "snapshot": 1730000010.0, "lat": 51.47, "lon": -0.45, "alt_m": 10668.0,
     "gs_mps": 230.5, "track_deg": 95.0, "vr_mps": 0.0, "on_ground": false,
     "source": "opensky"}

"t" is when the position was measured. "snapshot" is the time of the
surveillance picture (one OpenSky poll) the message belongs to; the engine
runs one detection cycle per snapshot.
"""

from __future__ import annotations

from typing import Any

# Index of each field in an OpenSky state vector (OpenSky REST API docs).
ICAO24 = 0
CALLSIGN = 1
TIME_POSITION = 3
LAST_CONTACT = 4
LONGITUDE = 5
LATITUDE = 6
BARO_ALTITUDE = 7
ON_GROUND = 8
VELOCITY = 9
TRUE_TRACK = 10
VERTICAL_RATE = 11

REQUIRED_FIELDS = ("icao24", "t", "snapshot", "lat", "lon", "alt_m", "gs_mps", "track_deg", "vr_mps")


def make_message(
    *,
    icao24: str,
    callsign: str,
    t: float,
    snapshot: float,
    lat: float,
    lon: float,
    alt_m: float,
    gs_mps: float,
    track_deg: float,
    vr_mps: float,
    on_ground: bool,
    source: str,
) -> dict[str, Any]:
    return {
        "icao24": icao24,
        "callsign": callsign,
        "t": float(t),
        "snapshot": float(snapshot),
        "lat": float(lat),
        "lon": float(lon),
        "alt_m": float(alt_m),
        "gs_mps": float(gs_mps),
        "track_deg": float(track_deg),
        "vr_mps": float(vr_mps),
        "on_ground": bool(on_ground),
        "source": source,
    }


def from_opensky(row: list[Any], snapshot_time: float) -> dict[str, Any] | None:
    """Convert one OpenSky state vector into a flights.states message.

    Returns None when the row lacks something detection needs. Barometric
    altitude is used, not GPS altitude: separation between flight levels is
    defined on pressure altitude, and mixing the two can be out by hundreds
    of feet.
    """
    if len(row) <= VERTICAL_RATE:
        return None

    icao24 = row[ICAO24]
    lat, lon = row[LATITUDE], row[LONGITUDE]
    alt, speed, track = row[BARO_ALTITUDE], row[VELOCITY], row[TRUE_TRACK]
    if not icao24 or any(value is None for value in (lat, lon, alt, speed, track)):
        return None

    # Fall back to the last message time when no position time is given.
    position_time = row[TIME_POSITION] if row[TIME_POSITION] is not None else row[LAST_CONTACT]
    if position_time is None:
        return None

    vertical_rate = row[VERTICAL_RATE]
    return make_message(
        icao24=str(icao24).strip().lower(),
        callsign=(row[CALLSIGN] or "").strip(),
        t=position_time,
        snapshot=snapshot_time,
        lat=lat,
        lon=lon,
        alt_m=alt,
        gs_mps=speed,
        track_deg=track % 360.0,
        vr_mps=vertical_rate if vertical_rate is not None else 0.0,  # Unknown: assume level
        on_ground=bool(row[ON_GROUND]),
        source="opensky",
    )


def missing_fields(message: dict[str, Any]) -> list[str]:
    """Names of required fields that are absent or null."""
    return [name for name in REQUIRED_FIELDS if message.get(name) is None]
