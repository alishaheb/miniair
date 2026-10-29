#ifndef MINIAIR_KAFKA_IO_HPP
#define MINIAIR_KAFKA_IO_HPP

#include <memory>
#include <optional>
#include <string>

#include <librdkafka/rdkafkacpp.h>

namespace miniair {

/// Thin wrapper over the librdkafka consumer. Errors are returned, not thrown.
class KafkaConsumer {
public:
    KafkaConsumer() = default;
    ~KafkaConsumer();
    KafkaConsumer(const KafkaConsumer&) = delete;
    KafkaConsumer& operator=(const KafkaConsumer&) = delete;
    KafkaConsumer(KafkaConsumer&&) = delete;
    KafkaConsumer& operator=(KafkaConsumer&&) = delete;

    /// Returns false and fills error on failure.
    bool connect(const std::string& brokers, const std::string& group_id, const std::string& topic,
                 std::string& error);

    /// Wait up to timeout_ms for one message. Empty when nothing arrived.
    std::optional<std::string> poll(int timeout_ms);

    void close();

private:
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
};

/// Thin wrapper over the librdkafka producer. Errors are returned, not thrown.
class KafkaProducer {
public:
    KafkaProducer() = default;
    ~KafkaProducer();
    KafkaProducer(const KafkaProducer&) = delete;
    KafkaProducer& operator=(const KafkaProducer&) = delete;
    KafkaProducer(KafkaProducer&&) = delete;
    KafkaProducer& operator=(KafkaProducer&&) = delete;

    bool connect(const std::string& brokers, std::string& error);

    /// Queue one message. Delivery failures are logged by a callback.
    bool send(const std::string& topic, const std::string& key, const std::string& payload);

    void flush(int timeout_ms);

private:
    class DeliveryLogger : public RdKafka::DeliveryReportCb {
    public:
        void dr_cb(RdKafka::Message& message) override;
    };

    // Declared before producer_ so it outlives it.
    DeliveryLogger delivery_logger_;
    std::unique_ptr<RdKafka::Producer> producer_;
};

}  // namespace miniair

#endif  // MINIAIR_KAFKA_IO_HPP
