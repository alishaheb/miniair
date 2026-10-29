# Safety-critical coding rules: a MISRA C++ subset

MISRA C++ is the coding standard used for safety-related C++ in aerospace, automotive and defence. The full standard is a paid document, and full compliance needs a certified checker and a formal process. This project does **not** claim MISRA compliance. It adopts a subset of the rules that fit a small C++17 code base, enforces as many as possible with free tools, and records every deviation, as a real project would.

MISRA C++:2023 is written against C++17, which is why the engine targets C++17.

Rule references are to MISRA C++:2008, whose numbering is widely cited; MISRA C++:2023 has equivalent rules. They show where each rule comes from and should be checked against the standard itself before being quoted.

## 1. Rules adopted

| # | Rule | Source | How it is enforced |
|---|---|---|---|
| R1 | The bodies of `if`, `else`, `for`, `while` and `switch` are always compound statements (braces) | 6-3-1, 6-4-1 | clang-tidy `readability-braces-around-statements`; review |
| R2 | Every `switch` has a final `default` clause | 6-4-6 | Review (every switch in the code has one) |
| R3 | No C-style casts; conversions are explicit `static_cast` | 5-2-4 | GCC `-Wold-style-cast -Werror`; clang-tidy `cppcoreguidelines-pro-type-cstyle-cast` |
| R4 | Casts do not remove `const` or `volatile` | 5-2-5 | GCC `-Wcast-qual`; clang-tidy `cppcoreguidelines-pro-type-const-cast` (one deviation, D1) |
| R5 | No implicit narrowing or sign-changing conversions | Chapter 5 conversion rules | GCC `-Wconversion -Wsign-conversion -Werror` |
| R6 | Floating-point values are never compared for equality | 6-2-2 | GCC `-Wfloat-equal -Werror`; tests use `EXPECT_NEAR` |
| R7 | The return value of a non-void function is used, or discarded explicitly with `(void)` | 0-1-7 | `[[nodiscard]]` on queries; explicit `(void)` casts; review |
| R8 | No recursion | 7-5-4 | Review (the code base has none) |
| R9 | `atof`, `atoi` and `atol` are not used; numeric input is parsed with full error checking | 18-0-2 | Review: `strtod` / `strtoul` with end-pointer checks in `main.cpp` |
| R10 | No `goto` (MISRA restricts it; this project bans it); no `<cstdio>` stream I/O | 6-6-1, 6-6-2, 27-0-1 | Review |
| R11 | Fixed-width integer types where size matters (`std::uint32_t`, `std::uint8_t` enum bases) | 3-9-2 (advisory) | Review |
| R12 | No raw `new` / `delete`; every resource is owned by RAII (`std::unique_ptr`, containers) | Resource-management guidance | clang-tidy `cppcoreguidelines-owning-memory`; review |
| R13 | Exceptions are not used for control flow; external input is parsed without throwing | Chapter 15 exception rules | `nlohmann::json::parse(..., allow_exceptions=false)`; types checked before access; `dump()` with error handler `replace`; tests assert `EXPECT_NO_THROW` |
| R14 | Variables are initialised where they are declared; struct members have default initialisers | 8-5-1 | GCC `-Wall -Wextra`; clang-tidy `cppcoreguidelines-init-variables`, `pro-type-member-init` |
| R15 | Scoped enumerations with explicit underlying types | Enum rules | Review (`enum class Severity : std::uint8_t`) |
| R16 | Single-purpose headers with include guards; no other macros except the build option `MINIAIR_WITH_KAFKA` | Chapter 16 preprocessor rules | Review |
| R17 | No identifier hides one in an outer scope; polymorphic bases have virtual destructors | 2-10-2 | GCC `-Wshadow -Wnon-virtual-dtor -Werror` |

Run-time checks back up the static rules. All tests also run under **AddressSanitizer** (out-of-bounds access, use-after-free, leaks) and **UndefinedBehaviorSanitizer** (overflow, invalid shifts, misaligned access) in CI.

## 2. Scope

* Applies to: `engine/include` and `engine/src` (the production C++ code).
* Not applied to: unit tests (they use GoogleTest macros and build with `-Wall -Wextra -Wpedantic` only); third-party code (nlohmann/json, librdkafka, GoogleTest); the Python services.

## 3. Deviation log

Each deviation is marked in the code with a comment naming it, and with a targeted `NOLINT` where a tool would flag it.

| ID | Rule | Location | Justification | Risk control |
|---|---|---|---|---|
| D1 | R4 (cast removes `const`) | `KafkaProducer::send`, `src/kafka_io.cpp` | The librdkafka C++ API takes a non-const `void*` payload. With `RK_MSG_COPY` the library copies the bytes and never writes through the pointer. | Flag fixed to `RK_MSG_COPY`; one call site; comment and `NOLINT` at the line. |
| D2 | Use of `<csignal>` (18-7-1); non-const global | `g_stop_requested`, `src/main.cpp` | Stopping cleanly on SIGTERM from Docker needs a signal handler. A handler may only safely write to a `volatile std::sig_atomic_t`, which must be global to reach it. | The handler only sets the flag; the main loop polls it. |
| D3 | Dynamic memory (18-4-1) | Standard containers in `TrackStore`, `AlertManager`, `ConflictDetector::detect` | A fixed-capacity design would need a hard limit on traffic numbers. For a ground-based, non-certified demonstrator, standard containers are the clearer choice. | No raw `new`/`delete`; containers are sized with `reserve()` where the size is known; memory is bounded by aircraft count, and stale tracks are pruned every cycle. A certified build would use fixed-capacity containers. |
| D4 | Third-party code not assessed | nlohmann/json, librdkafka | Widely used, actively maintained libraries. | Strict warnings apply only to project targets; the JSON library is used only in non-throwing modes. |
