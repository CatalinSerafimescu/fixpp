#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/log_capture.hpp — a Logger over one in-memory Sink, for
// SessionConfig::logger_override, and the records of one format it captured.
// 093-inbound-frame-dispositions (the session's log records, data-model E-12).

#include <chrono>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <utility>
#include <vector>

namespace fixpp::test_support {

class CaptureSink final : public fixpp::log::Sink {
public:
    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const& rec) noexcept override {
        std::scoped_lock lk{mu_};
        records_.push_back(rec);
    }
    void flush(std::chrono::milliseconds /*deadline*/) noexcept override {}
    void close() noexcept override {}
    [[nodiscard]] std::vector<fixpp::log::Record> records() const {
        std::scoped_lock lk{mu_};
        return records_;
    }

private:
    mutable std::mutex mu_;
    std::vector<fixpp::log::Record> records_;
};

// A Logger over one CaptureSink, for SessionConfig::logger_override.
struct LogCapture {
    CaptureSink* sink = nullptr;  // owned by `logger`
    std::shared_ptr<fixpp::log::Logger> logger;

    LogCapture() {
        auto owned = std::make_unique<CaptureSink>();
        sink = owned.get();
        std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks(std::pmr::get_default_resource());
        sinks.push_back(std::move(owned));
        logger = std::make_shared<fixpp::log::Logger>(fixpp::log::LoggerConfig{}, std::move(sinks));
    }

    // The records of one format (its FIXPP_FORMAT_ID) the session wrote. The logger
    // drains on its own thread, so this shuts it down first: every record enqueued
    // before the call is then counted.
    [[nodiscard]] std::vector<fixpp::log::Record> records_of(std::uint32_t format_id) {
        (void)logger->shutdown();
        std::vector<fixpp::log::Record> out;
        for (auto const& r : sink->records()) {
            if (r.format_id == format_id) out.push_back(r);
        }
        return out;
    }
};

}  // namespace fixpp::test_support
