// miniair-engine: reads aircraft state messages, runs conflict detection
// once per surveillance snapshot, and publishes alert events.
//
//   --input stdin   JSON lines on stdin, alerts as JSON lines on stdout
//   --input kafka   consume flights.states, produce conflicts.alerts

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "miniair/engine.hpp"
#include "miniair/geo.hpp"
#include "miniair/message_codec.hpp"

#ifdef MINIAIR_WITH_KAFKA
#include "miniair/kafka_io.hpp"
#endif

namespace {

constexpr int kExitOk = 0;
constexpr int kExitRuntimeError = 1;
constexpr int kExitUsage = 2;
constexpr std::uint64_t kMaxLoggedRejects = 10U;

// Deviation D2 (see docs/misra-subset.md): a signal handler may only safely
// write to a volatile sig_atomic_t, and it has to be global to be reached.
volatile std::sig_atomic_t g_stop_requested = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" void handle_stop_signal(int /*signal*/) { g_stop_requested = 1; }

struct Options {
    std::string input{"stdin"};
    std::string brokers{"localhost:9092"};
    std::string states_topic{"flights.states"};
    std::string alerts_topic{"conflicts.alerts"};
    std::string group_id{"miniair-engine"};
    int idle_flush_ms{500};
    miniair::EngineConfig engine;
};

void print_usage() {
    std::cerr << "Usage: miniair-engine [options]\n"
                 "  --input stdin|kafka      Where to read states from (default stdin)\n"
                 "  --brokers HOST:PORT      Kafka bootstrap servers (default localhost:9092)\n"
                 "  --states-topic NAME      Input topic (default flights.states)\n"
                 "  --alerts-topic NAME      Output topic (default conflicts.alerts)\n"
                 "  --group ID               Kafka consumer group (default miniair-engine)\n"
                 "  --idle-flush-ms N        Run a cycle after N ms with no input (default 500)\n"
                 "  --horizontal-nm N        Horizontal separation minimum (default 5)\n"
                 "  --vertical-ft N          Vertical separation minimum (default 1000)\n"
                 "  --look-ahead-s N         Prediction horizon (default 120)\n"
                 "  --min-altitude-ft N      Ignore traffic below this altitude (default 1000)\n"
                 "  --level-flight-fpm N     Treat smaller vertical rates as level (default 150)\n"
                 "  --max-track-age-s N      Drop tracks not updated for N s (default 60)\n"
                 "  --clear-after N          Conflict-free cycles before CLEARED (default 2)\n";
}

// strtod/strtoul with full-string checks, so bad input is rejected instead of
// being read as zero, and no exceptions are involved.
bool parse_positive_double(const char* text, double& out) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if ((end == text) || (*end != '\0') || (!(value > 0.0))) {
        return false;
    }
    out = value;
    return true;
}

bool parse_non_negative_double(const char* text, double& out) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if ((end == text) || (*end != '\0') || (!(value >= 0.0))) {
        return false;
    }
    out = value;
    return true;
}

bool parse_count(const char* text, std::uint32_t& out) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if ((end == text) || (*end != '\0') || (value == 0UL) || (value > 1000000UL)) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

std::optional<Options> parse_args(const std::vector<std::string>& args) {
    Options options;
    for (std::size_t i = 0U; i < args.size(); ++i) {
        const std::string& flag = args[i];
        if ((flag == "--help") || (flag == "-h")) {
            return std::nullopt;
        }
        if ((i + 1U) >= args.size()) {
            std::cerr << "Missing value for " << flag << '\n';
            return std::nullopt;
        }
        ++i;
        const char* const value = args[i].c_str();
        bool ok = true;
        std::uint32_t idle_ms = 0U;
        if (flag == "--input") {
            options.input = value;
            ok = (options.input == "stdin") || (options.input == "kafka");
        } else if (flag == "--brokers") {
            options.brokers = value;
        } else if (flag == "--states-topic") {
            options.states_topic = value;
        } else if (flag == "--alerts-topic") {
            options.alerts_topic = value;
        } else if (flag == "--group") {
            options.group_id = value;
        } else if (flag == "--idle-flush-ms") {
            ok = parse_count(value, idle_ms);
            options.idle_flush_ms = static_cast<int>(idle_ms);
        } else if (flag == "--horizontal-nm") {
            ok = parse_positive_double(value, options.engine.detector.horizontal_nm);
        } else if (flag == "--vertical-ft") {
            ok = parse_positive_double(value, options.engine.detector.vertical_ft);
        } else if (flag == "--look-ahead-s") {
            ok = parse_positive_double(value, options.engine.detector.look_ahead_s);
        } else if (flag == "--min-altitude-ft") {
            ok = parse_non_negative_double(value, options.engine.detector.min_altitude_ft);
        } else if (flag == "--level-flight-fpm") {
            ok = parse_non_negative_double(value, options.engine.level_flight_fpm);
        } else if (flag == "--max-track-age-s") {
            ok = parse_positive_double(value, options.engine.max_track_age_s);
        } else if (flag == "--clear-after") {
            ok = parse_count(value, options.engine.clear_after_cycles);
        } else {
            std::cerr << "Unknown option " << flag << '\n';
            return std::nullopt;
        }
        if (!ok) {
            std::cerr << "Invalid value '" << value << "' for " << flag << '\n';
            return std::nullopt;
        }
    }
    return options;
}

void log_config(const Options& options) {
    const miniair::DetectorConfig& d = options.engine.detector;
    std::cerr << "[engine] input=" << options.input << " minima=" << d.horizontal_nm << "NM/" << d.vertical_ft
              << "ft look_ahead=" << d.look_ahead_s << "s min_alt=" << d.min_altitude_ft
              << "ft level_band=" << options.engine.level_flight_fpm << "fpm max_track_age=" << options.engine.max_track_age_s
              << "s\n";
}

// Human-readable line for the operator log, for example:
// [ALERT] NEW PREDICTED MNA101/MNA102 in 118.9 s, CPA 0.00 NM / 0 ft
void log_event(const miniair::AlertEvent& event) {
    const miniair::Conflict& c = event.conflict;
    const std::string name_a = c.callsign_a.empty() ? c.icao24_a : c.callsign_a;
    const std::string name_b = c.callsign_b.empty() ? c.icao24_b : c.callsign_b;
    if (event.kind == miniair::AlertKind::Cleared) {
        std::cerr << "[ALERT] CLEARED " << name_a << '/' << name_b << '\n';
        return;
    }
    std::cerr << std::fixed << std::setprecision(1) << "[ALERT] " << miniair::to_string(event.kind) << ' '
              << miniair::to_string(c.severity) << ' ' << name_a << '/' << name_b << " in "
              << c.time_to_conflict_s << " s, CPA " << std::setprecision(2)
              << miniair::geo::m_to_nm(c.cpa_horizontal_m) << " NM / " << std::setprecision(0)
              << miniair::geo::m_to_ft(c.cpa_vertical_m) << " ft\n"
              << std::defaultfloat;
}

using EventSink = std::function<void(const miniair::AlertEvent&)>;

// Passes events to the sink, and logs a line whenever a cycle has run.
class Pipeline {
public:
    Pipeline(const miniair::EngineConfig& config, EventSink sink) : engine_{config}, sink_{std::move(sink)} {}

    void on_message(const std::string& text) {
        const miniair::ParseResult result = miniair::parse_state_report(text);
        if (!result.report.has_value()) {
            ++rejected_;
            if (rejected_ <= kMaxLoggedRejects) {
                std::cerr << "[engine] rejected message: " << result.error << '\n';
            }
            return;
        }
        ++accepted_;
        handle(engine_.on_report(result.report.value()));
    }

    void flush() { handle(engine_.flush()); }

    void log_totals() const {
        std::cerr << "[engine] accepted=" << accepted_ << " rejected=" << rejected_
                  << " cycles=" << engine_.cycles_run() << " active_alerts=" << engine_.active_alerts() << '\n';
    }

private:
    void handle(const std::vector<miniair::AlertEvent>& events) {
        if (engine_.cycles_run() != logged_cycles_) {
            logged_cycles_ = engine_.cycles_run();
            const miniair::CycleSummary& s = engine_.last_cycle();
            std::cerr << std::fixed << std::setprecision(1) << "[engine] cycle t=" << s.time_s
                      << " tracks=" << s.tracks << " conflicts=" << s.conflicts << " events=" << s.events << '\n'
                      << std::defaultfloat;
        }
        for (const miniair::AlertEvent& event : events) {
            log_event(event);
            sink_(event);
        }
    }

    miniair::Engine engine_;
    EventSink sink_;
    std::uint64_t accepted_{0U};
    std::uint64_t rejected_{0U};
    std::uint64_t logged_cycles_{0U};
};

int run_stdin(const Options& options) {
    Pipeline pipeline(options.engine, [](const miniair::AlertEvent& event) {
        std::cout << miniair::to_json(event) << '\n' << std::flush;
    });
    std::string line;
    while ((g_stop_requested == 0) && std::getline(std::cin, line)) {
        if (!line.empty()) {
            pipeline.on_message(line);
        }
    }
    pipeline.flush();
    pipeline.log_totals();
    return kExitOk;
}

#ifdef MINIAIR_WITH_KAFKA
int run_kafka(const Options& options) {
    std::string error;
    miniair::KafkaConsumer consumer;
    if (!consumer.connect(options.brokers, options.group_id, options.states_topic, error)) {
        std::cerr << "[engine] cannot create Kafka consumer: " << error << '\n';
        return kExitRuntimeError;
    }
    miniair::KafkaProducer producer;
    if (!producer.connect(options.brokers, error)) {
        std::cerr << "[engine] cannot create Kafka producer: " << error << '\n';
        return kExitRuntimeError;
    }
    std::cerr << "[engine] consuming " << options.states_topic << ", producing " << options.alerts_topic << " via "
              << options.brokers << '\n';

    Pipeline pipeline(options.engine, [&producer, &options](const miniair::AlertEvent& event) {
        // Key by pair so every event for one conflict lands on one partition, in order.
        const std::string key = event.conflict.icao24_a + "|" + event.conflict.icao24_b;
        (void)producer.send(options.alerts_topic, key, miniair::to_json(event));
    });

    while (g_stop_requested == 0) {
        const std::optional<std::string> message = consumer.poll(options.idle_flush_ms);
        if (message.has_value()) {
            pipeline.on_message(message.value());
        } else {
            // A quiet spell means the current snapshot has probably all
            // arrived, so run its cycle now instead of waiting for the next one.
            pipeline.flush();
        }
    }
    pipeline.flush();
    producer.flush(5000);
    consumer.close();
    pipeline.log_totals();
    return kExitOk;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    // Copy the arguments once, so the rest of the code avoids raw pointer arithmetic.
    const std::vector<std::string> args(std::next(argv), std::next(argv, argc));
    const std::optional<Options> options = parse_args(args);
    if (!options.has_value()) {
        print_usage();
        return kExitUsage;
    }
    (void)std::signal(SIGINT, handle_stop_signal);
    (void)std::signal(SIGTERM, handle_stop_signal);
    log_config(options.value());

    if (options->input == "kafka") {
#ifdef MINIAIR_WITH_KAFKA
        return run_kafka(options.value());
#else
        std::cerr << "[engine] built without Kafka support (MINIAIR_WITH_KAFKA=OFF)\n";
        return kExitUsage;
#endif
    }
    return run_stdin(options.value());
}
