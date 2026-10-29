"""Command-line entry point.

Every option can also be set with an environment variable, which is how
Docker Compose configures the service. OpenSky credentials are read only
from the environment, so they never appear in shell history or config files.
"""

from __future__ import annotations

import argparse
import logging
import os
import signal
import sys
import time
from collections.abc import Callable, Iterator, Sequence
from pathlib import Path

from miniair_ingest import replay, synthetic
from miniair_ingest.opensky import BoundingBox, OpenSkyClient, OpenSkyError, RateLimited
from miniair_ingest.schema import from_opensky
from miniair_ingest.sinks import KafkaSink, Sink, StdoutSink, encode
from miniair_ingest.synthetic import Snapshot

log = logging.getLogger("miniair_ingest")

# Southern England, including the London TMA. 10 square degrees = 1 credit a call.
DEFAULT_BBOX = "50.5,-2.5,53.0,1.5"
MAX_BACKOFF_S = 300.0


def _env_flag(name: str, default: bool) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def build_parser() -> argparse.ArgumentParser:
    env = os.environ.get
    parser = argparse.ArgumentParser(prog="miniair-ingest", description=__doc__.splitlines()[0])
    parser.add_argument(
        "--source", choices=["opensky", "synthetic", "replay"], default=env("MINIAIR_SOURCE", "synthetic")
    )
    parser.add_argument("--sink", choices=["kafka", "stdout"], default=env("MINIAIR_SINK", "stdout"))
    parser.add_argument("--bootstrap-servers", default=env("KAFKA_BOOTSTRAP_SERVERS", "localhost:9092"))
    parser.add_argument("--topic", default=env("MINIAIR_STATES_TOPIC", "flights.states"))
    parser.add_argument("--bbox", default=env("MINIAIR_BBOX", DEFAULT_BBOX), help="lamin,lomin,lamax,lomax")
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=float(env("MINIAIR_POLL_INTERVAL_S", "30")),
        help="Seconds between OpenSky polls",
    )
    parser.add_argument("--replay-file", type=Path, default=env("MINIAIR_REPLAY_FILE"))
    parser.add_argument("--record", type=Path, default=env("MINIAIR_RECORD_FILE"), help="Also append messages here")
    parser.add_argument(
        "--realtime",
        action=argparse.BooleanOptionalAction,
        default=_env_flag("MINIAIR_REALTIME", False),
        help="Pace synthetic/replay data like a live feed",
    )
    parser.add_argument(
        "--loop",
        action=argparse.BooleanOptionalAction,
        default=_env_flag("MINIAIR_LOOP", False),
        help="Restart the synthetic scenario when it ends",
    )
    parser.add_argument("--start-time", type=float, default=None, help="Synthetic start time (default: now)")
    parser.add_argument("--max-snapshots", type=int, default=None, help="Stop after this many snapshots")
    parser.add_argument("--log-level", default=env("MINIAIR_LOG_LEVEL", "INFO"))
    return parser


def opensky_snapshots(
    client: OpenSkyClient,
    box: BoundingBox,
    poll_interval_s: float,
    sleep: Callable[[float], None] = time.sleep,
    clock: Callable[[], float] = time.monotonic,
) -> Iterator[Snapshot]:
    """Poll OpenSky for ever, yielding each new snapshot. Errors are logged
    and retried with back-off, so a network blip does not stop the service."""
    last_time: float | None = None
    backoff_s = poll_interval_s
    while True:
        started = clock()
        try:
            snapshot_time, rows = client.fetch_states(box)
        except RateLimited as exc:
            log.warning("%s", exc)
            sleep(exc.retry_after_s)
            continue
        except OpenSkyError as exc:
            log.warning("OpenSky poll failed: %s (retrying in %.0f s)", exc, backoff_s)
            sleep(backoff_s)
            backoff_s = min(backoff_s * 2.0, MAX_BACKOFF_S)
            continue
        backoff_s = poll_interval_s

        if snapshot_time != last_time:  # OpenSky repeats a snapshot if polled too fast
            last_time = snapshot_time
            messages = [m for m in (from_opensky(row, snapshot_time) for row in rows) if m is not None]
            log.info(
                "OpenSky snapshot t=%.0f: %d aircraft, %d usable, credits left=%s",
                snapshot_time,
                len(rows),
                len(messages),
                client.remaining_credits,
            )
            yield snapshot_time, messages
        sleep(max(0.0, poll_interval_s - (clock() - started)))


def build_source(args: argparse.Namespace) -> Iterator[Snapshot]:
    if args.source == "synthetic":
        start = args.start_time if args.start_time is not None else float(int(time.time()))
        return synthetic.snapshots(start, realtime=args.realtime, loop=args.loop)
    if args.source == "replay":
        if args.replay_file is None:
            raise SystemExit("--source replay needs --replay-file (or MINIAIR_REPLAY_FILE)")
        return replay.snapshots(Path(args.replay_file), realtime=args.realtime)

    box = BoundingBox.parse(args.bbox)
    client = OpenSkyClient(
        client_id=os.environ.get("OPENSKY_CLIENT_ID"),
        client_secret=os.environ.get("OPENSKY_CLIENT_SECRET"),
    )
    daily_calls = 86400.0 / args.poll_interval
    budget = 4000 if client.authenticated else 400
    log.info(
        "OpenSky %s, box %s (%.0f sq deg, %d credit/call), polling every %.0f s (~%.0f credits/day of %d)",
        "authenticated" if client.authenticated else "anonymous",
        args.bbox,
        box.area_sq_deg,
        box.credit_cost,
        args.poll_interval,
        daily_calls * box.credit_cost,
        budget,
    )
    return opensky_snapshots(client, box, args.poll_interval)


def build_sink(args: argparse.Namespace) -> Sink:
    if args.sink == "kafka":
        log.info("publishing to Kafka topic %s via %s", args.topic, args.bootstrap_servers)
        return KafkaSink(args.bootstrap_servers, args.topic)
    return StdoutSink()


def run(source: Iterator[Snapshot], sink: Sink, record: Path | None = None, max_snapshots: int | None = None) -> int:
    """Copy snapshots from source to sink. Returns the number of snapshots sent."""
    recorder = record.open("a", encoding="utf-8") if record else None
    sent = 0
    try:
        for snapshot_time, messages in source:
            count = sink.send(messages)
            if recorder:
                recorder.writelines(encode(m) + "\n" for m in messages)
                recorder.flush()
            sent += 1
            log.debug("snapshot t=%.0f: sent %d messages", snapshot_time, count)
            if max_snapshots is not None and sent >= max_snapshots:
                break
    finally:
        sink.close()
        if recorder:
            recorder.close()
    return sent


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    # Logs go to stderr: stdout may be carrying data into the engine.
    logging.basicConfig(
        level=args.log_level.upper(), stream=sys.stderr, format="%(asctime)s [ingest] %(levelname)s %(message)s"
    )
    # Docker stops containers with SIGTERM; exit cleanly so Kafka is flushed.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    try:
        sent = run(build_source(args), build_sink(args), args.record, args.max_snapshots)
    except KeyboardInterrupt:
        return 0
    except ValueError as exc:
        log.error("%s", exc)
        return 2
    log.info("done: %d snapshots sent", sent)
    return 0


if __name__ == "__main__":
    sys.exit(main())
