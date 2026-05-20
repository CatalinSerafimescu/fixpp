// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/memory_store_factory.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::MemoryStoreFactory.
// Anchor: .specify/2e-msgstore.md v0.4 §4.4.
//
// `final` impl of MessageStoreFactory. make() enforces the storage-DoS
// construction guard against EngineConfig::max_store_memory_per_session
// (round-2 N9 / C-R2-P1-3 / I-11). MemoryStoreFactory holds a reference to
// the EngineConfig so make() can validate without the caller re-passing
// max_store_memory_per_session.
#pragma once

#include <memory>
#include <memory_resource>
#include <string_view>

#include <fixpp/core/error.hpp>                          // expected_t
#include <fixpp/session/memory_store.hpp>                // MemoryStore::Config
#include <fixpp/session/message_store_factory.hpp>       // MessageStoreFactory

namespace fixpp::core { struct EngineConfig; }

namespace fixpp::session {

class MemoryStoreFactory final : public MessageStoreFactory {
public:
    MemoryStoreFactory(MemoryStore::Config cfg,
                       const fixpp::core::EngineConfig& engine_cfg) noexcept;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr) noexcept override;
};

}  // namespace fixpp::session
