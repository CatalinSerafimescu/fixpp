// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/quickfix_compat/cfg_loader.hpp
//
// fixpp::session::quickfix_compat::cfg_loader — QuickFIX .cfg → FileStoreFactory
// config translation helper.
//
// Anchor: .specify/2e-msgstore.md v0.4 §4.8.A.2 (line 869). FR-030.
// Entity E12.  Task T046.
//
// CONFIG TRANSLATION ONLY — NO runtime adapter.  Path A retired in v0.3 per
// Codex C-R2-P2-1 escalation ([2e §4.8.B]).  The five hazards listed in
// [2e §4.8.A.1] (user callback re-entry deadlock, hidden std::mutex, unbounded
// blocking on sync I/O, mismatched threading model, cancellation token mismatch)
// compose; a static_assert-wrapped "safe subset" cannot enforce them against
// user attestation.  v1.0 ships Path B (documented incompatibility + migration
// recipe) + cfg_loader (config translation).
//
// The §4.8.A.3 1-page migration recipe lives in
// book/migration_from_quickfix.md (FR-031).  The compile-time Path B guard
// (FR-032) lives in tests/session/test_quickfix_compat_path_b_guard.cpp (T044).
//
// Mirror of specs/008-message-store/contracts/cfg_loader.hpp (shape oracle).
#pragma once

#include <filesystem>
#include <fixpp/core/error.hpp>                  // expected_t
#include <fixpp/session/file_store_factory.hpp>  // FileStoreFactory
#include <memory>

namespace fixpp::session::quickfix_compat {

// Reads a QuickFIX .cfg file ([DEFAULT] / [SESSION] block format),
// extracts FileStorePath, SenderCompID, TargetCompID, and emits an equivalent
// FileStoreFactory.  The factory is returned by unique_ptr; caller owns it and
// passes it into SessionConfig::store_factory.
//
// The returned FileStoreFactory carries a FileStore::Config whose
// file_io_executor is left default-constructed (empty).  The engine populates
// it from EngineConfig::file_io_executor at FileStoreFactory::make() time
// (see contracts/file_store_factory.hpp); this preserves the Config-only-CTOR
// contract per design-doc §4.4 frozen surface AND the FileStore::Config::
// file_io_executor required-at-construction contract per [2e §4.3.2]:665
// (FileStore is constructed inside make()).
//
// Errors: file-not-found, parse-failure, malformed key, missing required key,
// or CompID filesystem-safety validation failure all return
// expected_t::unexpected{store_factory_failed} (slot 61).
// No new error variants are introduced (FR-021 freeze).
//
// CompID defense-in-depth (v0.5 per [2e §D.4] — Gap 1 close):
// cfg_to_file_store_factory validates SenderCompID and TargetCompID with the
// same rule set as FileStoreFactory::make() — reject empty, path separators,
// NUL, `.`/`..` segments, control chars [0x00,0x1F]/0x7F, NAME_MAX excess —
// returning store_factory_failed at config-load time before any session is
// opened.  The same validation re-runs at make() time for direct Path-B users
// who bypass cfg_loader.
[[nodiscard]] fixpp::core::expected_t<std::unique_ptr<FileStoreFactory>> cfg_to_file_store_factory(
    const std::filesystem::path& cfg_path) noexcept;

}  // namespace fixpp::session::quickfix_compat
