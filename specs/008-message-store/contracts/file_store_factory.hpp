// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/file_store_factory.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::FileStoreFactory.
// Anchor: .specify/2e-msgstore.md v0.4 §4.4 (lines 727–732).
//
// `final` impl of MessageStoreFactory. Config-only constructor per the
// design-doc-frozen public surface. The storage-DoS construction guard
// (I-11) is enforced at make() time against the `max_store_memory_bytes`
// value the engine threads in as make()'s 4th parameter (see
// contracts/message_store_factory.hpp). The factory CTOR does NOT take
// an EngineConfig& back-channel.
//
// The file_io_executor injection contract (FR-024 / I-13 / research D-7):
// FileStore::Config::file_io_executor is required-at-construction on the
// FileStore itself per [2e §4.3.2]:665. FileStore is constructed inside
// make(), so the executor must be present at make() time. Resolution
// rule (Config-supplied wins):
//   1. If the FileStoreFactory's stored Config.file_io_executor is
//      non-empty, that caller-supplied executor is used (Path-B user
//      shape — caller passed their own executor at factory construction).
//   2. Otherwise, the engine-threaded `file_io_executor` parameter
//      (make()'s 5th argument, sourced from EngineConfig::file_io_executor
//      per design-doc §4.3.2:669) populates the minted FileStore::Config.
//   3. If both are empty, make() returns store_factory_failed (the
//      engine MUST supply an executor or the caller MUST set it on the
//      Config — there is no "no executor" operating mode).
// This preserves the design-doc §4.4 Config-only-CTOR contract AND the
// [2e §4.3.2]:665 required-at-construction contract on FileStore itself,
// because FileStore is constructed inside make() once the executor is
// resolved.
//
// make() also opens the live log, takes the advisory flock / LockFileEx
// (I-16), runs the restart algorithm (I-14), verifies the sentinel
// record's session_triple_hash.
#pragma once

#include <memory>
#include <memory_resource>
#include <string_view>

#include <asio/any_io_executor.hpp>

#include <fixpp/core/error.hpp>                          // expected_t
#include <fixpp/session/file_store.hpp>                  // FileStore::Config
#include <fixpp/session/message_store_factory.hpp>       // MessageStoreFactory

namespace fixpp::session {

class FileStoreFactory final : public MessageStoreFactory {
public:
    explicit FileStoreFactory(FileStore::Config c) noexcept;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr,
         std::size_t max_store_memory_bytes,
         asio::any_io_executor file_io_executor) noexcept override;
};

}  // namespace fixpp::session
