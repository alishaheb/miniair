# MiniAir backlog

Work is planned in three sprints. Each item is written to become one GitHub issue (or Jira ticket) on the project board, with its acceptance criteria as the issue's checklist. Suggested labels: `engine`, `ingestion`, `infra`, `docs`, `safety`, `sprint-1/2/3`.

Status: ✅ done · 🔲 to do

## Sprint 1: core engine (C++)

**Goal:** a tested C++ engine that finds conflicts in a traffic picture.

| ID | Story | Acceptance criteria | Status |
|---|---|---|---|
| S1-01 | As a developer, I want a CMake project with strict warnings so that problems fail the build. | C++17; `-Wall -Wextra -Wconversion ... -Werror` on project code; GoogleTest wired into CTest | ✅ |
| S1-02 | As the engine, I need geodesy helpers so that distances and positions are correct. | Haversine within 1 m of reference values; local plane within 0.1% of haversine at 100 km; extrapolation handles the antimeridian | ✅ |
| S1-03 | As a controller, I want loss of separation detected so that I know when minima are infringed now. | 5 NM and 1,000 ft, both at once; 1,000 ft exactly counts as separated | ✅ |
| S1-04 | As a controller, I want conflicts predicted two minutes ahead so that I can act early. | Conflict window = horizontal ∩ vertical ∩ look-ahead; climb-through uses the later entry time; CPA reported | ✅ |
| S1-05 | As an operator, I want one alert per conflict, not one per cycle. | NEW, UPDATED on severity change, CLEARED after 2 clear cycles | ✅ |
| S1-06 | As the engine, I need a track store so that each cycle uses a consistent picture. | Out-of-order reports ignored; stale tracks pruned; picture extrapolated to the cycle time | ✅ |
| S1-07 | As a maintainer, I want the pair search to scale so that busy airspace stays fast. | Latitude sweep; property test shows identical results to brute force on 400 aircraft | ✅ |
| S1-08 | As a safety reviewer, I want a documented coding standard so that I can assess the code. | MISRA subset written; deviations logged; clang-tidy clean; sanitiser build passes | ✅ |

## Sprint 2: integration

**Goal:** live data flows through Kafka to the engine, all in Docker.

| ID | Story | Acceptance criteria | Status |
|---|---|---|---|
| S2-01 | As the system, I want live ADS-B data from OpenSky so that the demo uses real traffic. | Bounding-box query; OAuth2 client credentials or anonymous; credit use logged | ✅ |
| S2-02 | As an operator, I want ingestion to survive outages so that it runs unattended. | Back-off on errors; waits on HTTP 429; token refresh on 401 | ✅ |
| S2-03 | As a tester, I want a synthetic scenario with known outcomes so that tests are exact. | 3 pairs (conflict, separated, climb-through); expected alerts documented | ✅ |
| S2-04 | As a tester, I want to record and replay sessions so that live findings can be reproduced. | `--record` and `--source replay`; real-time pacing optional | ✅ |
| S2-05 | As the system, I want Kafka between services so that they are decoupled. | `flights.states` and `conflicts.alerts`; topics created explicitly; keyed messages | ✅ |
| S2-06 | As a developer, I want one command to start everything. | `docker compose up`; services wait for a healthy broker | ✅ |
| S2-07 | As a team, I want CI on every push. | GitHub Actions: C++ (release and sanitisers), clang-tidy, Python lint and tests, system test | ✅ |
| S2-08 | As a reviewer, I want design and test documents. | Requirements with IDs; UML component, sequence and state diagrams; traceability matrix | ✅ |
| S2-09 | As a controller, I don't want false alerts from ADS-B vertical-rate noise. *(Found in live data.)* | Level-flight band applied at input; regression tests from the live case; recorded session replays without the false alert | ✅ |

## Sprint 3: alerting, deployment and tuning

**Goal:** an operator can see alerts on a dashboard, and the system runs in the cloud.

| ID | Story | Acceptance criteria | Status |
|---|---|---|---|
| S3-01 | As an operator, I want a Java alert service so that alerts reach a dashboard. | Spring Boot (or plain Java) Kafka consumer; active alerts held in memory; REST endpoint `GET /alerts`; JUnit tests | 🔲 |
| S3-02 | As an operator, I want a live dashboard. | Web page showing active alerts with severity colours; pushed via WebSocket or Server-Sent Events | 🔲 |
| S3-03 | As a controller, I don't want alerts for normal approach spacing. *(Found in live data: Gatwick final approach.)* | Inhibition zones around listed airports (radius and ceiling); unit tests; live replay shows the approach alert gone | 🔲 |
| S3-04 | As a controller, I want terminal-area minima. | 3 NM below a configurable altitude or inside listed terminal areas; tests at the boundary | 🔲 |
| S3-05 | As the team, I want the system deployed on AWS. | EC2 (free tier) running Docker Compose, or ECS; deployment steps documented; secrets in AWS Systems Manager Parameter Store | 🔲 |
| S3-06 | As the team, I want images built and pushed by CI. | GitHub Actions pushes tagged images to GHCR or ECR on merge to `main` | 🔲 |
| S3-07 | As an operator, I want health and metrics. | Cycle time, track count and alert count logged or exposed; container health checks | 🔲 |
| S3-08 | As a maintainer, I want turn-aware prediction considered. | Short design note comparing straight-line and turn-rate prediction; decision recorded | 🔲 |

## Definition of done

* Code reviewed and merged through a pull request.
* All CI checks green.
* Requirement IDs updated in the design document, and the traceability matrix updated in the test plan.
* Any new deviation from the MISRA subset logged.
