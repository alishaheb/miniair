"""Check the alerts that came out of Kafka against the synthetic scenario.

Usage: python3 check_alerts.py alerts.jsonl

The expected events are documented in ingestion/miniair_ingest/synthetic.py.
Standard library only, so it runs on any CI runner.
"""

from __future__ import annotations

import json
import sys

START = 1700000000.0

# (seconds after start, event, pair, severity)
EXPECTED = {
    (5.0, "NEW", ("4d0301", "4d0302"), "PREDICTED"),
    (70.0, "NEW", ("4d0101", "4d0102"), "PREDICTED"),
    (125.0, "UPDATED", ("4d0301", "4d0302"), "LOSS_OF_SEPARATION"),
    (140.0, "CLEARED", ("4d0301", "4d0302"), "LOSS_OF_SEPARATION"),
    (190.0, "UPDATED", ("4d0101", "4d0102"), "LOSS_OF_SEPARATION"),
    (255.0, "CLEARED", ("4d0101", "4d0102"), "LOSS_OF_SEPARATION"),
}


def load(path: str) -> list[dict]:
    alerts = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line.startswith("{"):
                alerts.append(json.loads(line))
    return alerts


def main(path: str) -> int:
    alerts = load(path)
    actual = {
        (a["cycle_time"] - START, a["event"], (a["icao24_a"], a["icao24_b"]), a["severity"])
        for a in alerts
    }

    print(f"{'t+s':>6}  {'event':<8} {'pair':<16} severity")
    for t, event, pair, severity in sorted(actual):
        mark = "ok " if (t, event, pair, severity) in EXPECTED else "!! "
        print(f"{mark}{t:>4.0f}  {event:<8} {pair[0]}/{pair[1]}  {severity}")

    missing = EXPECTED - actual
    unexpected = actual - EXPECTED
    for item in sorted(missing):
        print(f"MISSING:    {item}")
    for item in sorted(unexpected):
        print(f"UNEXPECTED: {item}")
    if len(alerts) != len(actual):
        print(f"DUPLICATES: {len(alerts)} messages for {len(actual)} distinct events")

    if missing or unexpected or len(alerts) != len(actual):
        print("FAIL")
        return 1
    print(f"PASS: all {len(EXPECTED)} expected alert events received, nothing else")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
