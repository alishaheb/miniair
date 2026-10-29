"""A scripted traffic scenario with known outcomes, for demos and tests.

Three pairs of aircraft, each pair 150 km from the others so they never
interact:

* MNA101 / MNA102 - cross at right angles at FL350.       Expected: conflict.
* MNA201 / MNA202 - same crossing, but 2,000 ft apart.     Expected: no conflict.
* MNA301 / MNA302 - one climbs through the other's level.  Expected: conflict.

With 5 s snapshots and the default engine settings (5 NM, 1,000 ft, 120 s
look-ahead), the engine should report:

* MNA301/MNA302: NEW (predicted) at t=5 s, UPDATED (loss) at t=125 s, CLEARED at t=140 s
* MNA101/MNA102: NEW (predicted) at t=70 s, UPDATED (loss) at t=190 s, CLEARED at t=255 s
"""

from __future__ import annotations

import math
import time
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from typing import Any

from miniair_ingest.schema import make_message

EARTH_RADIUS_M = 6371000.0
FEET_TO_M = 0.3048
SCENARIO_ORIGIN = (52.0, -1.0)  # Over the English Midlands
GROUP_SPACING_M = 150000.0

Snapshot = tuple[float, list[dict[str, Any]]]


@dataclass(frozen=True)
class Flight:
    icao24: str
    callsign: str
    east_m: float  # Start position relative to the scenario origin
    north_m: float
    alt_ft: float
    speed_mps: float
    track_deg: float
    vr_mps: float = 0.0

    def state_at(self, elapsed_s: float, snapshot_time: float) -> dict[str, Any]:
        track = math.radians(self.track_deg)
        east = self.east_m + self.speed_mps * math.sin(track) * elapsed_s
        north = self.north_m + self.speed_mps * math.cos(track) * elapsed_s
        lat0, lon0 = SCENARIO_ORIGIN
        lat = lat0 + math.degrees(north / EARTH_RADIUS_M)
        # Scale east-west distance at the aircraft's own latitude, so that
        # position changes match the stated ground speed.
        lon = lon0 + math.degrees(east / (EARTH_RADIUS_M * math.cos(math.radians(lat))))
        return make_message(
            icao24=self.icao24,
            callsign=self.callsign,
            t=snapshot_time,
            snapshot=snapshot_time,
            lat=lat,
            lon=lon,
            alt_m=self.alt_ft * FEET_TO_M + self.vr_mps * elapsed_s,
            gs_mps=self.speed_mps,
            track_deg=self.track_deg,
            vr_mps=self.vr_mps,
            on_ground=False,
            source="synthetic",
        )


FLIGHTS: tuple[Flight, ...] = (
    # Right-angle crossing at FL350: both reach the crossing point together.
    Flight("4d0101", "MNA101", -50000.0, 0.0, 35000.0, 230.0, 90.0),
    Flight("4d0102", "MNA102", 0.0, -50000.0, 35000.0, 230.0, 0.0),
    # Same geometry 150 km north, but 2,000 ft apart: safely separated.
    Flight("4d0201", "MNA201", -50000.0, GROUP_SPACING_M, 35000.0, 230.0, 90.0),
    Flight("4d0202", "MNA202", 0.0, GROUP_SPACING_M - 50000.0, 37000.0, 230.0, 0.0),
    # 150 km south: MNA301 climbs at 1,500 ft/min through MNA302's level.
    Flight("4d0301", "MNA301", -25000.0, -GROUP_SPACING_M, 30000.0, 220.0, 90.0, vr_mps=7.62),
    Flight("4d0302", "MNA302", 25000.0, -GROUP_SPACING_M, 34000.0, 220.0, 270.0),
)


def snapshots(
    start_time: float,
    duration_s: float = 300.0,
    step_s: float = 5.0,
    realtime: bool = False,
    loop: bool = False,
    sleep: Callable[[float], None] = time.sleep,
) -> Iterator[Snapshot]:
    """Yield (snapshot time, messages) every step_s seconds of scenario time.

    realtime=True waits step_s between snapshots, like a live feed.
    loop=True restarts the scenario when it ends, for a long-running demo.
    """
    run_start = start_time
    while True:
        steps = int(duration_s // step_s)
        for i in range(steps + 1):
            elapsed = i * step_s
            snapshot_time = run_start + elapsed
            yield snapshot_time, [flight.state_at(elapsed, snapshot_time) for flight in FLIGHTS]
            if realtime:
                sleep(step_s)
        if not loop:
            return
        run_start += (steps + 1) * step_s
