# MiniAir test plan

| | |
|---|---|
| Version | 0.1 (end of Sprint 2) |
| Related | [Design document](design.md) (requirement IDs) · [CI workflow](../.github/workflows/ci.yml) |

## 1. Approach

Testing follows the V-model. Each requirement in the design document is checked at the lowest level that can show it works, and the whole chain is then checked end to end. Every check that can be automated runs in CI on every push.

Verification methods (standard systems-engineering terms):

* **T** Test: an automated test passes.
* **D** Demonstration: the system is run and the behaviour observed.
* **I** Inspection: code, configuration or documents are reviewed.
* **A** Analysis: reasoning or calculation shows the requirement is met.

## 2. Test levels

| Level | What | Tool | Where | Count |
|---|---|---|---|---|
| Unit (C++) | Geometry, detector, track store, alert manager, codec, engine cycle | GoogleTest | `engine/tests/` | 88 |
| Component (C++) | Engine executable on the recorded synthetic scenario; checks all 6 alert events and their exact cycle times | CTest + CMake script | `engine/tests/cli_test.cmake` | 1 |
| Unit (Python) | Schema conversion, OpenSky client (fake HTTP session), synthetic scenario, replay, polling loop | pytest | `ingestion/tests/` | 41 |
| System | Full Docker Compose stack: ingestion to Kafka to engine to Kafka; checks the exact alert set on `conflicts.alerts` | Bash + Python | `tests/integration/` | 1 scenario, 6 events |
| Static analysis | clang-tidy (bugprone, CERT, C++ Core Guidelines, performance, readability); strict compiler warnings as errors; ruff for Python | clang-tidy, GCC, ruff | CI | - |
| Dynamic analysis | All C++ tests under AddressSanitizer and UndefinedBehaviorSanitizer | GCC sanitisers | CI matrix | - |
| Live data | Live OpenSky session recorded and replayed through the engine | `--record`, replay | manual | - |

### Test oracles

* Geometry tests use reference values calculated independently (for example, haversine distances worked out separately), not values read back from the code under test.
* The synthetic scenario is designed so the expected alerts can be worked out by hand. Its docstring (`ingestion/miniair_ingest/synthetic.py`) states them, and both the component test and the system test check against that list.
* The latitude-sweep optimisation is checked against a brute-force all-pairs search on 400 random aircraft (a property-style test with a fixed seed).

## 3. Environments

| Environment | Used for |
|---|---|
| Ubuntu 24.04 container (`engine/Dockerfile`, `toolchain` stage) | Local C++ build, tests, clang-tidy, sanitisers |
| `python:3.12-slim` container | Local Python tests and lint |
| GitHub Actions `ubuntu-24.04` | CI: all automated levels |
| Docker Compose (Kafka 3.9, KRaft) | System test and demos |

## 4. Entry and exit criteria

**Entry**: code builds with zero warnings under `-Werror`.

**Exit** (every push):

* all unit, component and system tests pass;
* clang-tidy reports no findings;
* sanitiser build passes;
* ruff lint and format checks pass;
* any new nuisance-alert finding from live data has a regression test or a backlog item.

## 5. Traceability matrix

| Req | Method | Verified by |
|---|---|---|
| REQ-F-01 OpenSky input | T, D | `test_opensky.py` (query parameters, anonymous and OAuth2 requests, token caching and refresh); live session demonstration |
| REQ-F-02 Normalise, drop incomplete | T | `test_schema.py`: `test_converts_a_complete_row`, `test_rows_missing_detection_fields_are_dropped`, `test_uses_barometric_not_geometric_altitude` |
| REQ-F-03 One message per aircraft, keyed | I, T | `KafkaSink` inspection; system test |
| REQ-F-04 Track store rules | T | `TrackStore.*` (6 tests); `Engine.StaleTracksAreDropped` |
| REQ-F-05 Cycle per snapshot | T | `Engine.NoCycleRunsUntilTheSnapshotIsComplete`, `Engine.FlushRunsThePendingCycleOnce`, `Engine.LateReportDoesNotMoveTheClockBackwards`, `TrackStore.PictureMovesEveryTrackToTheSameTime` |
| REQ-F-06 Loss of separation | T | `ConflictDetector.AlreadyTooCloseIsLossOfSeparation`, `NineHundredFeetApartIsLossOfSeparation`, `ExactlyOneThousandFeetApartIsSeparated` |
| REQ-F-07 Predicted conflict | T | `ConflictDetector.HeadOnAtSameLevelIsPredicted`, `RightAngleCrossingIsPredicted`, `ClimbThroughUsesTheVerticalEntryTime`, `ClimbThroughIsMissedWhenLookAheadIsTooShort`, `ConflictBeyondLookAheadIsIgnored`, `CpaIsLimitedToTheLookAheadWindow`, `VerticallySeparatedCrossingIsNotAConflict`, `ParallelTracksTenMilesApartAreNotAConflict`, `DivergingAircraftAreNotAConflict`, `LatitudeSweepFindsTheSameConflictsAsBruteForce` |
| REQ-F-08 Ignore ground / low traffic | T | `ConflictDetector.DetectIgnoresAircraftOnTheGroundOrTooLow` |
| REQ-F-09 Alert lifecycle | T | `AlertManager.*` (8 tests); `Engine.HeadOnEncounterGoesNewThenUpdatedThenCleared`; `cli_synthetic_scenario`; system test |
| REQ-F-10 Alert JSON in aviation units | T | `MessageCodec.AlertIsSerialisedInAviationUnits`; system test |
| REQ-F-11 Configurable parameters | T, D | `ConflictDetector.CustomMinimaAreApplied`, `ConflictBeyondLookAheadIsIgnored`, `Engine.WithoutTheLevelBandTheSameNoiseRaisesAnAlert`; invalid CLI values rejected with exit code 2 (demonstrated) |
| REQ-F-12 Synthetic and replay sources | T | `test_synthetic.py` (8 tests), `test_replay_and_app.py` (replay tests) |
| REQ-N-01 Bad input never crashes | T | `MessageCodec.RejectsMalformedJsonWithoutThrowing`, `RejectsNonObjects`, `RejectsOutOfRangeValues`, 24 parameterised missing/null/wrong-type field tests; `test_replay_skips_bad_and_incomplete_lines` |
| REQ-N-02 No exceptions for control flow | T, I | `MessageCodec.RejectsMalformedJsonWithoutThrowing`, `InvalidUtf8InCallsignDoesNotThrow`; [MISRA subset](misra-subset.md) review |
| REQ-N-03 Deterministic | T | `cli_synthetic_scenario` and system test both check exact cycle times |
| REQ-N-04 Cycle time | A, T | O(n log n) sort plus a bounded sweep (design section 5.3); the 400-aircraft sweep test, including a brute-force comparison, runs in a few milliseconds |
| REQ-N-05 Clean shutdown | D | Engine in Kafka mode stopped with SIGTERM exits 0 after flushing (demonstrated); Python service exits through `sys.exit` so `finally` flushes Kafka (inspection) |
| REQ-N-06 Survive errors and rate limits | T | `test_polling_survives_errors_and_skips_repeated_snapshots`, `test_429_raises_rate_limited_with_retry_after`, `test_401_refreshes_the_token_once_and_retries`, `test_network_failure_raises_opensky_error` |
| REQ-N-07 No credentials in code | I | Credentials read only from `OPENSKY_CLIENT_ID` / `OPENSKY_CLIENT_SECRET`; `.env` is git-ignored; no CLI flag for secrets |
| REQ-N-08 Runs on Linux containers | T, D | System test (CI); `docker compose up` |
| REQ-N-09 No false alerts from vertical-rate noise | T, D | `Engine.AdsbVerticalRateNoiseDoesNotRaiseFalseAlerts`, `Engine.WithoutTheLevelBandTheSameNoiseRaisesAnAlert`, `Engine.RealClimbAboveTheLevelBandIsStillPredicted`, `ConflictDetector.ExactlyOneThousandFeetApartIsSeparated`; replay of the recorded live session |

## 6. How to run

```bash
make test          # C++ and Python unit tests (in Docker)
make lint-engine   # clang-tidy
make system-test   # full stack through Kafka
```

CI runs the same steps, plus the sanitiser build, on every push.

## 7. Results (end of Sprint 2)

| Level | Result |
|---|---|
| C++ unit and component (89) | Pass, also under ASan and UBSan |
| Python unit (41) | Pass |
| System test | Pass: 6 of 6 expected alert events, no others, in about 30 s |
| clang-tidy | No findings (2 justified suppressions, see the deviation log) |
| Live data | 2 nuisance alerts found: 1 fixed (REQ-N-09), 1 on the backlog (airport zones) |
