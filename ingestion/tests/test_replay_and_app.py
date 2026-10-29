import json

from miniair_ingest import app, replay, synthetic
from miniair_ingest.opensky import BoundingBox, OpenSkyError, RateLimited
from miniair_ingest.sinks import StdoutSink, encode

START = 1700000000.0


def write_jsonl(path, messages):
    path.write_text("".join(encode(m) + "\n" for m in messages), encoding="utf-8")


def scenario_messages(duration_s=10.0):
    return [m for _, batch in synthetic.snapshots(START, duration_s=duration_s) for m in batch]


# ---- Replay ----------------------------------------------------------------


def test_replay_groups_lines_by_snapshot(tmp_path):
    path = tmp_path / "session.jsonl"
    write_jsonl(path, scenario_messages(duration_s=10.0))

    snaps = list(replay.snapshots(path))

    assert [t for t, _ in snaps] == [START, START + 5.0, START + 10.0]
    assert all(len(batch) == len(synthetic.FLIGHTS) for _, batch in snaps)


def test_replay_skips_bad_and_incomplete_lines(tmp_path):
    good = scenario_messages(duration_s=0.0)
    incomplete = dict(good[0])
    del incomplete["lat"]
    path = tmp_path / "session.jsonl"
    path.write_text("not json\n\n" + json.dumps(incomplete) + "\n" + encode(good[1]) + "\n", encoding="utf-8")

    snaps = list(replay.snapshots(path))
    assert len(snaps) == 1
    assert snaps[0][1] == [good[1]]


def test_replay_realtime_keeps_original_gaps(tmp_path):
    path = tmp_path / "session.jsonl"
    write_jsonl(path, scenario_messages(duration_s=10.0))
    sleeps = []
    list(replay.snapshots(path, realtime=True, sleep=sleeps.append))
    assert sleeps == [5.0, 5.0]


# ---- OpenSky polling loop --------------------------------------------------


class ScriptedClient:
    """Stands in for OpenSkyClient: returns or raises from a script."""

    remaining_credits = 100

    def __init__(self, script):
        self.script = list(script)

    def fetch_states(self, box):
        item = self.script.pop(0)
        if isinstance(item, Exception):
            raise item
        return item


def opensky_row(icao24):
    return [icao24, "TEST1", "UK", 100, 100, -1.0, 52.0, 10000.0, False, 200.0, 90.0, 0.0, None, 10100.0]


def test_polling_survives_errors_and_skips_repeated_snapshots():
    client = ScriptedClient(
        [
            (100.0, [opensky_row("aaa001"), ["bad"]]),
            OpenSkyError("network down"),
            (100.0, [opensky_row("aaa001")]),  # Same snapshot again: not re-sent
            RateLimited(60.0),
            (110.0, [opensky_row("aaa001"), opensky_row("aaa002")]),
        ]
    )
    sleeps = []
    stream = app.opensky_snapshots(client, BoundingBox(50, -2, 53, 1), 10.0, sleep=sleeps.append, clock=lambda: 0.0)

    first = next(stream)
    second = next(stream)

    assert first[0] == 100.0 and [m["icao24"] for m in first[1]] == ["aaa001"]
    assert second[0] == 110.0 and len(second[1]) == 2
    assert 10.0 in sleeps and 60.0 in sleeps  # Back-off after the error, then the rate-limit wait


# ---- End to end through main() --------------------------------------------


def test_run_sends_every_snapshot_and_records_it(tmp_path, capsys):
    record = tmp_path / "recorded.jsonl"
    sent = app.run(synthetic.snapshots(START, duration_s=10.0), StdoutSink(), record=record)

    lines = capsys.readouterr().out.splitlines()
    assert sent == 3
    assert len(lines) == 3 * len(synthetic.FLIGHTS)
    assert record.read_text(encoding="utf-8").splitlines() == lines


def test_run_stops_after_max_snapshots(capsys):
    assert app.run(synthetic.snapshots(START), StdoutSink(), max_snapshots=2) == 2
    assert len(capsys.readouterr().out.splitlines()) == 2 * len(synthetic.FLIGHTS)


def test_main_synthetic_to_stdout(capsys):
    assert (
        app.main(["--source", "synthetic", "--sink", "stdout", "--start-time", str(START), "--max-snapshots", "1"]) == 0
    )
    lines = capsys.readouterr().out.splitlines()
    assert len(lines) == len(synthetic.FLIGHTS)
    assert json.loads(lines[0])["snapshot"] == START


def test_main_rejects_bad_bounding_box():
    assert app.main(["--source", "opensky", "--bbox", "53,0,50,1"]) == 2
