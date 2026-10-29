#include "miniair/kafka_io.hpp"

#include <iostream>
#include <utility>
#include <vector>

namespace miniair {

namespace {

using Settings = std::vector<std::pair<std::string, std::string>>;

std::unique_ptr<RdKafka::Conf> make_conf(const Settings& settings, std::string& error) {
    std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    for (const auto& setting : settings) {
        if (conf->set(setting.first, setting.second, error) != RdKafka::Conf::CONF_OK) {
            return nullptr;
        }
    }
    return conf;
}

}  // namespace

KafkaConsumer::~KafkaConsumer() { close(); }

bool KafkaConsumer::connect(const std::string& brokers, const std::string& group_id, const std::string& topic,
                            std::string& error) {
    const std::unique_ptr<RdKafka::Conf> conf = make_conf(
        {
            {"bootstrap.servers", brokers},
            {"group.id", group_id},
            {"auto.offset.reset", "earliest"},
            {"enable.partition.eof", "false"},
        },
        error);
    if (!conf) {
        return false;
    }
    consumer_.reset(RdKafka::KafkaConsumer::create(conf.get(), error));
    if (!consumer_) {
        return false;
    }
    const RdKafka::ErrorCode result = consumer_->subscribe({topic});
    if (result != RdKafka::ERR_NO_ERROR) {
        error = RdKafka::err2str(result);
        consumer_.reset();
        return false;
    }
    return true;
}

std::optional<std::string> KafkaConsumer::poll(int timeout_ms) {
    if (!consumer_) {
        return std::nullopt;
    }
    const std::unique_ptr<RdKafka::Message> message(consumer_->consume(timeout_ms));
    if (!message) {
        return std::nullopt;
    }
    switch (message->err()) {
        case RdKafka::ERR_NO_ERROR: {
            const char* const data = static_cast<const char*>(message->payload());
            if (data == nullptr) {
                return std::string{};
            }
            return std::string(data, message->len());
        }
        case RdKafka::ERR__TIMED_OUT:
            return std::nullopt;
        default:
            std::cerr << "[engine] Kafka consume error: " << message->errstr() << '\n';
            return std::nullopt;
    }
}

void KafkaConsumer::close() {
    if (consumer_) {
        (void)consumer_->close();
        consumer_.reset();
    }
}

void KafkaProducer::DeliveryLogger::dr_cb(RdKafka::Message& message) {
    if (message.err() != RdKafka::ERR_NO_ERROR) {
        std::cerr << "[engine] Kafka delivery failed: " << message.errstr() << '\n';
    }
}

KafkaProducer::~KafkaProducer() { flush(5000); }

bool KafkaProducer::connect(const std::string& brokers, std::string& error) {
    const std::unique_ptr<RdKafka::Conf> conf = make_conf({{"bootstrap.servers", brokers}}, error);
    if (!conf) {
        return false;
    }
    if (conf->set("dr_cb", &delivery_logger_, error) != RdKafka::Conf::CONF_OK) {
        return false;
    }
    producer_.reset(RdKafka::Producer::create(conf.get(), error));
    return static_cast<bool>(producer_);
}

bool KafkaProducer::send(const std::string& topic, const std::string& key, const std::string& payload) {
    if (!producer_) {
        return false;
    }
    // Deviation D1 (see docs/misra-subset.md): the librdkafka C++ API takes a
    // non-const payload pointer. With RK_MSG_COPY it copies the bytes and
    // never writes through the pointer, so casting away const is safe here.
    const RdKafka::ErrorCode result = producer_->produce(
        topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
        const_cast<char*>(payload.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        payload.size(), key.data(), key.size(), 0, nullptr);
    (void)producer_->poll(0);  // Serve delivery callbacks
    if (result != RdKafka::ERR_NO_ERROR) {
        std::cerr << "[engine] Kafka produce failed: " << RdKafka::err2str(result) << '\n';
        return false;
    }
    return true;
}

void KafkaProducer::flush(int timeout_ms) {
    if (producer_) {
        (void)producer_->flush(timeout_ms);
    }
}

}  // namespace miniair
