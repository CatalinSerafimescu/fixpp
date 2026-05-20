// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/file_store_factory.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::FileStoreFactory.
// Anchor: .specify/2e-msgstore.md v0.4 §4.4.
//
// `final` impl of MessageStoreFactory. make() enforces the storage-DoS
// construction guard (I-11), opens the live log, takes the advisory flock /
// LockFileEx (I-16), runs the restart algorithm (I-14), verifies the
// sentinel record's session_triple_hash.
#pragma once

#include <memory>
#include <memory_resource>
#include <string_view>

#include <fixpp/core/error.hpp>                          // expected_t
#include <fixpp/session/file_store.hpp>                  // FileStore::Config
#include <fixpp/session/message_store_factory.hpp>       // MessageStoreFactory

namespace fixpp::core { struct EngineConfig; }

namespace fixpp::session {

class FileStoreFactory final : public MessageStoreFactory {
public:
    FileStoreFactory(FileStore::Config cfg,
                     const fixpp::core::EngineConfig& engine_cfg) noexcept;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr) noexcept override;
};

}  // namespace fixpp::session
