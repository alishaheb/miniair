#!/usr/bin/env bash
# System test: runs the full stack (Kafka, ingestion, engine) in Docker
# Compose, feeds the synthetic scenario through Kafka, and checks the exact
# alert events that come out of the conflicts.alerts topic.
set -euo pipefail

cd "$(dirname "$0")/../.."
here=tests/integration
compose=(docker compose -p miniair-it -f docker-compose.yml -f "$here/docker-compose.test.yml")
alerts_file="$here/alerts.jsonl"
expected_events=6
deadline=$((SECONDS + 120))

cleanup() {
    status=$?
    if [ "$status" -ne 0 ]; then
        echo "---- engine and ingestion logs ----"
        "${compose[@]}" logs --no-color engine ingestion || true
    fi
    "${compose[@]}" down -v --remove-orphans > /dev/null 2>&1 || true
    exit "$status"
}
trap cleanup EXIT

consume_alerts() {
    "${compose[@]}" exec -T kafka /opt/kafka/bin/kafka-console-consumer.sh \
        --bootstrap-server localhost:9092 --topic conflicts.alerts \
        --from-beginning --timeout-ms 5000 2> /dev/null || true
}

echo "==> Building images and starting the stack"
"${compose[@]}" up -d --build --wait kafka
"${compose[@]}" up -d --build engine ingestion

echo "==> Waiting for ingestion to publish the scenario"
"${compose[@]}" wait ingestion > /dev/null

echo "==> Waiting for ${expected_events} alert events"
while :; do
    consume_alerts > "$alerts_file"
    count=$(grep -c '^{' "$alerts_file" || true)
    if [ "$count" -ge "$expected_events" ] || [ "$SECONDS" -ge "$deadline" ]; then
        break
    fi
    sleep 2
done

echo "==> Received ${count} alert events"
python3 "$here/check_alerts.py" "$alerts_file"
