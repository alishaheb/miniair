# ---- Toolchain: also used by CI and `make engine-shell` ----
FROM ubuntu:24.04 AS toolchain
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config clang-tidy \
      librdkafka-dev nlohmann-json3-dev libgtest-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src

# ---- Build and test: the image build fails if any test fails ----
FROM toolchain AS build
COPY CMakeLists.txt .clang-tidy ./
COPY include include
COPY src src
COPY tests tests
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build \
 && ctest --test-dir build --output-on-failure

# ---- Runtime: just the binary and the Kafka client library ----
FROM ubuntu:24.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends librdkafka++1 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --no-create-home miniair
COPY --from=build /src/build/miniair-engine /usr/local/bin/miniair-engine
USER miniair
ENTRYPOINT ["miniair-engine"]
CMD ["--input", "kafka", "--brokers", "kafka:9092"]
