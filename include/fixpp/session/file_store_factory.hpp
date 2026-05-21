// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/file_store_factory.hpp
//
// fixpp::session::FileStoreFactory — factory producing FileStore instances.
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.4 (lines 727–732). Entity E7.
// FR-008 (log path) / FR-013 (advisory lock) / FR-014 (factory contract) /
// FR-024 (file_io_executor injection) / I-11 (storage-DoS) / I-13 /
// I-16 (advisory lock) / [2e §D.4] (CompID validation — Gap 1 close) /
// [2e §D.5] (filesystem-type scope restriction — Gap 2 close).
//
// make() MUST execute in this exact order — validation BEFORE any file op
// per [2e §D.4] Gap 1 close:
//   (1) CompID filesystem-safety validation (BEFORE any file op) — reject
//       empty / path separators / NUL / `.`/`..` segments / control chars
//       [0x00,0x1F] / 0x7F / NAME_MAX excess → store_factory_failed.
//       Validation uses ONLY string_view::find_first_of / find / size() —
//       NO std::filesystem::path constructors at validation time (N-5).
//   (2) Resolve file_io_executor (Config-supplied-wins):
//       Config non-empty → use it; else threaded-in 5th-param; both empty
//       → store_factory_failed.
//   (3) Take advisory exclusive open lock (flock Linux / LockFileEx Windows);
//       second opener → store_factory_failed.
//   (4) Construct the FileStore with the resolved Config.
//
// Filesystem-type scope restriction ([2e §D.5] — Gap 2 close):
// FileStore is supported only on filesystems where flock / LockFileEx
// provides effective cross-process exclusive-lock semantics (local FS).
// NFS / SMB / FUSE / cluster FS are unsupported in v1.0. make() does NOT
// detect or warn (probe-is-worse-than-nothing per [2e §D.5]); operators
// MUST verify out of band.
//
// Storage-DoS construction guard lands in US2 T030 — DO NOT add now.
//
// Mirror of specs/008-message-store/contracts/file_store_factory.hpp.
#pragma once

#include <asio/any_io_executor.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <memory>
#include <memory_resource>
#include <string_view>

namespace fixpp::session {

class FileStoreFactory final : public MessageStoreFactory {
public:
    explicit FileStoreFactory(FileStore::Config c) noexcept : cfg_(std::move(c)) {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view sender, std::string_view target, std::pmr::memory_resource* mr,
        std::size_t max_store_memory_bytes,
        asio::any_io_executor file_io_executor) noexcept override;

private:
    FileStore::Config cfg_;
};

}  // namespace fixpp::session
