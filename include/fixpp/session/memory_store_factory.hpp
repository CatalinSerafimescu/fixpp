// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/memory_store_factory.hpp
//
// fixpp::session::MemoryStoreFactory — factory producing MemoryStore instances.
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.4. Entity E6. FR-014.
//
// `final` impl of MessageStoreFactory. Config-only constructor per the
// design-doc-frozen public surface. The storage-DoS construction guard
// (round-2 N9 / C-R2-P1-3 / I-11) is enforced at make() time using the
// `max_store_memory_bytes` value the engine threads in as make()'s 4th
// parameter; the factory CTOR does NOT take an EngineConfig& back-channel.
//
// The make()-5th-parameter `file_io_executor` is ACCEPTED AND SILENTLY
// DISCARDED per E6 (MemoryStore has no file-I/O work; an empty or non-empty
// executor is contractually meaningless on this path — NOT a misuse).
//
// Storage-DoS construction guard implemented in US2 T030 (FR-014 / I-11):
//   (a) inbound_capacity + outbound_capacity overflow → store_factory_failed
//   (b) (sum > 0) && max_frame_bytes > max_store_memory_bytes / sum → fail
//   (c) max_frame_bytes > Framer::Config::max_frame_bytes (256 KiB) → fail
// Only applied under capacity_policy::bounded (unbounded is exempt).
//
// Mirror of specs/008-message-store/contracts/memory_store_factory.hpp.
#pragma once

#include <asio/any_io_executor.hpp>
#include <climits>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/wire/framer.hpp>  // default_max_frame_bytes (256 KiB cap per T030 / I-11)
#include <memory>
#include <memory_resource>
#include <string_view>

namespace fixpp::session {

class MemoryStoreFactory final : public MessageStoreFactory {
public:
    explicit MemoryStoreFactory(MemoryStore::Config c = {}) noexcept : cfg_(c) {}

    // MemoryStore is volatile (in-memory only) — not durable across restart.
    // Override the factory default (true) to false so Session::open() sets
    // store_is_persistent_ = false and ensure_hydrated_() skips the read
    // (029 C2.2 / research D-10 / INV-H4).
    [[nodiscard]] bool yields_persistent_store() const noexcept override { return false; }

    // make(): mint a MemoryStore for the given <sender, target> session.
    //
    // sender / target are accepted but NOT used by MemoryStore (it has no
    // on-disk state; the session identity is irrelevant for an in-memory
    // store).
    //
    // mr: if the Config.store_resource is nullptr AND mr is non-null, the
    // engine-provided mr is used for the store's internal storage (FR-026
    // peer-not-sub-resource rule: the store_arena's lifetime matches the
    // store instance, NOT the per-message session_arena reset cadence).
    //
    // max_store_memory_bytes: storage-DoS guard (T030 / FR-014 / I-11).
    //   For capacity_policy::bounded:
    //     (a) inbound_capacity + outbound_capacity overflow → store_factory_failed
    //     (b) sum > 0 && max_frame_bytes > max_store_memory_bytes / sum → fail
    //     (c) max_frame_bytes > Framer::Config::max_frame_bytes → fail
    //   capacity_policy::unbounded is exempt (grows at runtime; DoS not applicable).
    //
    // file_io_executor: accepted and silently discarded (E6 contract).
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* mr,
        std::size_t max_store_memory_bytes,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        // ── T030: storage-DoS construction guard (FR-014 / I-11) ────────────
        // Runtime out-of-range-cast reject for capacity_policy ([const §XV.15] /
        // I-09 closed-enum backstop). The enum is closed with exactly 2 values
        // (bounded=0, unbounded=1); a cast from an out-of-range integer
        // (FFI/SWIG bypass) is caught here before any capacity arithmetic.
        {
            const auto raw = static_cast<std::uint8_t>(cfg_.policy);
            if (raw != static_cast<std::uint8_t>(capacity_policy::bounded) &&
                raw != static_cast<std::uint8_t>(capacity_policy::unbounded)) {
                return std::unexpected(fixpp::core::error::store_factory_failed);
            }
        }

        // Only applied under bounded policy; unbounded grows at runtime.
        if (cfg_.policy == capacity_policy::bounded) {
            // (a) overflow-safe sum check: inbound_capacity + outbound_capacity
            const std::size_t ic = cfg_.inbound_capacity;
            const std::size_t oc = cfg_.outbound_capacity;
            // Overflow if ic + oc > SIZE_MAX, i.e., ic > SIZE_MAX - oc
            if (ic > SIZE_MAX - oc) {
                return std::unexpected(fixpp::core::error::store_factory_failed);
            }
            const std::size_t sum = ic + oc;

            // (b) product exceeds cap: sum > 0 && max_frame_bytes > cap / sum
            // Rearranged to avoid overflow: max_frame_bytes * sum > cap
            // equivalent: max_frame_bytes > cap / sum (integer division)
            if (sum > 0) {
                if (cfg_.max_frame_bytes > max_store_memory_bytes / sum) {
                    return std::unexpected(fixpp::core::error::store_factory_failed);
                }
            }

            // (c) max_frame_bytes exceeds Framer::Config::max_frame_bytes (256 KiB)
            // Per T030: max_frame_bytes > Framer::Config::max_frame_bytes → fail
            if (cfg_.max_frame_bytes > fixpp::wire::default_max_frame_bytes) {
                return std::unexpected(fixpp::core::error::store_factory_failed);
            }
        }

        // Apply engine-provided mr if the Config doesn't supply one (FR-026)
        MemoryStore::Config resolved_cfg = cfg_;
        if (resolved_cfg.store_resource == nullptr && mr != nullptr) {
            resolved_cfg.store_resource = mr;
        }

        // Construct the MemoryStore. std::make_unique uses new/delete
        // (default deleter) per [2e §D.6] — the v1.0 unique_ptr deleter contract.
        auto store = std::make_unique<MemoryStore>(resolved_cfg);
        return fixpp::core::expected_t<std::unique_ptr<MessageStore>>{std::move(store)};
    }

private:
    MemoryStore::Config cfg_;
};

}  // namespace fixpp::session
