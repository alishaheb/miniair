# Everything runs in Docker, so the only thing you need installed is Docker.

COMPOSE := docker compose
TOOLCHAIN := miniair-toolchain

.PHONY: help test test-engine test-ingestion lint-engine system-test demo up up-live down logs alerts diagrams clean

help: ## Show this help
	@grep -E '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*## "}; {printf "  make %-15s %s\n", $$1, $$2}'

test: test-engine test-ingestion ## Run all unit tests

test-engine: ## Build the C++ engine and run its tests (GoogleTest + CLI scenario)
	docker build --target build -t miniair/engine:test engine

lint-engine: ## Run clang-tidy on the engine
	docker build -q --target toolchain -t $(TOOLCHAIN) engine
	docker run --rm -v "$(CURDIR)/engine":/src:ro $(TOOLCHAIN) sh -c \
	  "cmake -S /src -B /tmp/b -G Ninja -DMINIAIR_BUILD_TESTS=OFF > /dev/null && clang-tidy -p /tmp/b --quiet /src/src/*.cpp"

test-ingestion: ## Run Python lint and tests
	docker run --rm -v "$(CURDIR)/ingestion":/app -w /app -e RUFF_CACHE_DIR=/tmp/ruff -e PYTHONDONTWRITEBYTECODE=1 \
	  python:3.12-slim sh -c "pip install -q --root-user-action=ignore -r requirements-dev.txt && \
	  ruff check . && ruff format --check . && pytest -q -p no:cacheprovider"

system-test: ## Full stack through Kafka, checks the exact alerts
	./tests/integration/run.sh

demo: ## Synthetic scenario piped straight into the engine (no Kafka)
	$(COMPOSE) build engine ingestion
	docker run --rm miniair/ingestion:dev --source synthetic --sink stdout --log-level WARNING \
	  | docker run --rm -i miniair/engine:dev --input stdin > /dev/null

up: ## Start the stack with the looping synthetic scenario
	$(COMPOSE) up -d --build
	@echo "Watch alerts with: make logs   or   make alerts"

up-live: ## Start the stack with live OpenSky data
	MINIAIR_SOURCE=opensky $(COMPOSE) up -d --build

down: ## Stop the stack and delete its data
	$(COMPOSE) down -v

logs: ## Follow the engine's operator log
	$(COMPOSE) logs -f engine

alerts: ## Print alert messages from the conflicts.alerts topic
	$(COMPOSE) exec kafka /opt/kafka/bin/kafka-console-consumer.sh \
	  --bootstrap-server localhost:9092 --topic conflicts.alerts --from-beginning

diagrams: ## Render the PlantUML diagrams to SVG
	docker run --rm -v "$(CURDIR)/docs/diagrams":/data plantuml/plantuml:1.2024.7 -tsvg /data/*.puml

clean: ## Remove build volumes and local images
	-docker volume rm miniair-build miniair-build-asan
	-docker image rm miniair/engine:dev miniair/engine:test miniair/ingestion:dev $(TOOLCHAIN)
