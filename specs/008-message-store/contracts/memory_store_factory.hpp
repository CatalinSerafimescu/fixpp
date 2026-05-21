// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/memory_store_factory.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::MemoryStoreFactory.
// Anchor: .specify/2e-msgstore.md v0.4 §4.4 (lines 720–725).
//
// `final` impl of MessageStoreFactory. The Config-only constructor matches
// the design-doc-frozen public surface. The storage-DoS construction guard
// (round-2 N9 / C-R2-P1-3 / I-11) is enforced at make() time using the
// `max_store_memory_bytes` value the engine threads in as make()'s 4th
// parameter (see contracts/message_store_factory.hpp); the factory CTOR
// does NOT take an EngineConfig& back-channel.
//
// The make()-5th-parameter `file_io_executor` is IGNORED on this path
// (MemoryStore has no file-I/O work; it accepts the parameter to satisfy
// the polymorphic MessageStoreFactory::make() signature but does not
// consume it). An empty/default-constructed executor is permitted; a
// non-empty executor is ALSO accepted and silently discarded (no-op,
// NOT a misuse — MemoryStore has no file-I/O work, so any executor
// value is contractually meaningless on this path; the engine threads
// the same `EngineConfig::file_io_executor` value into every factory's
// make() per FR-005, and the MemoryStore path discards it rather than
// branching on emptiness). Tests guard this behaviour at the
// MemoryStoreFactory section of seam 13 (cap-construction-guard +
// factory-accept-on-arbitrary-executor) per spec.md SC-004 / FR-014.
#pragma once

#include <memory>
#include <memory_resource>
#include <string_view>

#include <asio/any_io_executor.hpp>

#include <fixpp/core/error.hpp>                          // expected_t
#include <fixpp/session/memory_store.hpp>                // MemoryStore::Config
#include <fixpp/session/message_store_factory.hpp>       // MessageStoreFactory

namespace fixpp::session {

class MemoryStoreFactory final : public MessageStoreFactory {
public:
    explicit MemoryStoreFactory(MemoryStore::Config c = {}) noexcept;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr,
         std::size_t max_store_memory_bytes,
         asio::any_io_executor file_io_executor) noexcept override;
};

}  // namespace fixpp::session
