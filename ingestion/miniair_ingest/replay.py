"""Replay recorded flights.states messages from a JSON Lines file.

Record a live session once with --record, then replay it as often as you
like without spending OpenSky credits.
"""

from __future__ import annotations

import json
import logging
import time
from collections.abc import Callable, Iterator
from pathlib import Path

from miniair_ingest.schema import missing_fields
from miniair_ingest.synthetic import Snapshot

log = logging.getLogger(__name__)


def snapshots(
    path: Path,
    realtime: bool = False,
    sleep: Callable[[float], None] = time.sleep,
) -> Iterator[Snapshot]:
    """Yield (snapshot time, messages), grouping consecutive lines that share
    a snapshot time. With realtime=True, keep the original gaps between them."""
    current_time: float | None = None
    batch: list[dict] = []
    with path.open(encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                message = json.loads(line)
            except json.JSONDecodeError as exc:
                log.warning("%s:%d: skipping bad JSON (%s)", path, line_number, exc)
                continue
            if not isinstance(message, dict) or missing_fields(message):
                log.warning("%s:%d: skipping incomplete message", path, line_number)
                continue

            snapshot_time = float(message["snapshot"])
            if current_time is not None and snapshot_time != current_time:
                yield current_time, batch
                if realtime and snapshot_time > current_time:
                    sleep(snapshot_time - current_time)
                batch = []
            current_time = snapshot_time
            batch.append(message)

    if current_time is not None:
        yield current_time, batch
