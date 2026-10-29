"""Where messages go: Kafka for the full system, stdout for piping straight
into the engine (miniair-ingest ... | miniair-engine)."""

from __future__ import annotations

import json
import logging
import sys
from collections.abc import Iterable
from typing import Any, Protocol, TextIO

log = logging.getLogger(__name__)


def encode(message: dict[str, Any]) -> str:
    return json.dumps(message, separators=(",", ":"))


class Sink(Protocol):
    def send(self, messages: Iterable[dict[str, Any]]) -> int: ...

    def close(self) -> None: ...


class StdoutSink:
    def __init__(self, stream: TextIO | None = None) -> None:
        self._stream = stream if stream is not None else sys.stdout

    def send(self, messages: Iterable[dict[str, Any]]) -> int:
        count = 0
        for message in messages:
            self._stream.write(encode(message) + "\n")
            count += 1
        self._stream.flush()
        return count

    def close(self) -> None:
        self._stream.flush()


class KafkaSink:
    """Publishes one message per aircraft, keyed by ICAO address so each
    aircraft's reports stay in order."""

    def __init__(self, bootstrap_servers: str, topic: str) -> None:
        from confluent_kafka import Producer  # Imported here so tests need no Kafka

        self._topic = topic
        self._failed = 0
        self._producer = Producer(
            {
                "bootstrap.servers": bootstrap_servers,
                "enable.idempotence": True,  # No duplicates on retry
                "linger.ms": 20,
            }
        )

    def _on_delivery(self, error: Any, _message: Any) -> None:
        if error is not None:
            self._failed += 1
            log.error("Kafka delivery failed: %s", error)

    def send(self, messages: Iterable[dict[str, Any]]) -> int:
        count = 0
        for message in messages:
            payload = encode(message)
            try:
                self._producer.produce(self._topic, key=message["icao24"], value=payload, on_delivery=self._on_delivery)
            except BufferError:
                # Local queue full: let it drain, then try once more.
                self._producer.poll(1.0)
                self._producer.produce(self._topic, key=message["icao24"], value=payload, on_delivery=self._on_delivery)
            self._producer.poll(0)
            count += 1
        # Wait until the whole snapshot is written, so the engine sees it together.
        remaining = self._producer.flush(10.0)
        if remaining:
            log.warning("%d messages still waiting for Kafka after flush", remaining)
        return count

    def close(self) -> None:
        self._producer.flush(10.0)
        if self._failed:
            log.error("%d messages failed to reach Kafka", self._failed)
