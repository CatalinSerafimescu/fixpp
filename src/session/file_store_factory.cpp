// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/file_store_factory.cpp
//
// fixpp::session::FileStoreFactory::make() implementation.
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.4 / [2e §D.4] / [2e §D.5].
// FR-008 / FR-013 / FR-014 / FR-024 / I-11 / I-13 / I-16.
//
// make() order is FROZEN per [2e §D.4] Gap 1 close:
//   (1) CompID filesystem-safety validation (BEFORE any file op)
//   (2) Resolve file_io_executor
//   (3) Take advisory exclusive open lock
//   (4) Construct the FileStore
//
// CompID validation detail ([2e §D.4] / [2e §9 seam #2 N-3]):
//   reject: empty; path separator '/' (Linux) or '\\' (Windows); NUL byte '\0';
//   exact '.' or '..'; control chars [0x00,0x1F] or 0x7F;
//   composed path component > NAME_MAX (Linux: pathconf(_PC_NAME_MAX);
//   Windows: MAX_PATH - directory.native().size() - strlen("__") - strlen(".log")).
//   Implementation uses ONLY string_view::find_first_of / find / size() per N-5
//   (std::filesystem::path constructors NOT invoked at validation time — they can
//   throw on Windows long paths, breaking the noexcept make() contract).
//
// The validate_compid_filesystem_safety() helper is placed in the fixpp::session::detail
// namespace so it can be consumed by T047's cfg_loader (US4) without duplication.
//
// Scope-restriction documentation ([2e §D.5] — Gap 2 close):
// The make() impl does NOT detect or warn on unsupported filesystems (NFS / SMB /
// FUSE / cluster FS). See file_store_factory.hpp docstring and [2e §D.5].

#include <climits>
#include <cstdint>
#include <filesystem>
#include <fixpp/session/file_store_factory.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Shared CompID-validation predicate — declaration only; body follows in this
// TU so that platform-specific headers (<unistd.h> / <windows.h>) stay
// contained here rather than leaking through the header.
#include <fixpp/session/detail/validate_compid_filesystem_safety.hpp>

#ifndef _WIN32
#include <unistd.h>  // pathconf
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // MAX_PATH, CreateFileW, etc.
#endif

#include <fixpp/core/error.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/wire/framer.hpp>  // default_max_frame_bytes (256 KiB cap per T030 / I-11)

namespace fixpp::session {

// FileStoreImpl is defined in file_store.cpp — we only need FileStore::open_log()

namespace detail {

// validate_compid_filesystem_safety: checks a single CompID string for all
// filesystem-safety constraints per [2e §D.4]. Returns the error code to
// return if validation fails, or std::nullopt if the CompID is valid.
//
// IMPLEMENTATION DISCIPLINE (N-5 noexcept-safe primitives):
//   - Uses ONLY std::string_view::find_first_of / find / size()
//   - Does NOT call std::filesystem::path constructors at validation time
//   - Does NOT throw (noexcept-safe for make() noexcept contract)
[[nodiscard]] std::optional<fixpp::core::error> validate_compid_filesystem_safety(
    std::string_view compid,
    std::string_view directory_native,             // native path string for NAME_MAX calc
    std::string_view other_compid  // used to compute composed name length
    ) noexcept {
    // (1) Empty check
    if (compid.empty()) {
        return fixpp::core::error::store_factory_failed;
    }

    // (2) NUL byte check (string_view can hold embedded NUL)
    if (compid.find('\0') != std::string_view::npos) {
        return fixpp::core::error::store_factory_failed;
    }

    // (3) Path separator check
    // '/' is a separator on all platforms. '\\' is also a separator on Windows.
    if (compid.find('/') != std::string_view::npos) {
        return fixpp::core::error::store_factory_failed;
    }
#ifdef _WIN32
    if (compid.find('\\') != std::string_view::npos) {
        return fixpp::core::error::store_factory_failed;
    }
#endif

    // (4) Exact '.' or '..' check (the segment IS a dot segment)
    if (compid == "." || compid == "..") {
        return fixpp::core::error::store_factory_failed;
    }

    // (5) Control characters [0x00, 0x1F] and 0x7F check
    for (unsigned char c : compid) {
        if (c < 0x20u || c == 0x7Fu) {
            return fixpp::core::error::store_factory_failed;
        }
    }

    // (6) NAME_MAX check: composed path component is sender + "__" + target + ".log"
    // We compute the full composed component size and compare against NAME_MAX.
    // The composed component for the current compid (whether sender or target)
    // contributes: compid.size() (+ "__" (2) + other_compid.size() + ".log" (4))
    // other_compid.size() is the other component's length.
    const std::size_t composed_len = compid.size() + 2 +                       // "__"
                                     other_compid.size() + 4;  // ".log"

#ifndef _WIN32
    // Linux: use pathconf on the directory path
    {
        // pathconf requires a null-terminated string; directory_native is native
        // Note: directory_native is passed from the Config.directory.native() string.
        // We need a null-terminated version.
        std::string dir_str(directory_native);
        long name_max = ::pathconf(dir_str.c_str(), _PC_NAME_MAX);
        if (name_max < 0) {
            // pathconf failed (e.g., directory doesn't exist yet); use POSIX minimum
            name_max = 255;
        }
        if (composed_len > static_cast<std::size_t>(name_max)) {
            return fixpp::core::error::store_factory_failed;
        }
    }
#else  // _WIN32
    // Windows: MAX_PATH minus the directory prefix length
    {
        const std::size_t dir_len = directory_native.size();
        // Overhead: directory separator + sender + "__" + target + ".log" + NUL
        const std::size_t overhead = dir_len + 1 /* sep */ + composed_len + 1 /* NUL */;
        if (overhead > MAX_PATH) {
            return fixpp::core::error::store_factory_failed;
        }
    }
#endif

    return std::nullopt;  // valid
}

}  // namespace detail

// ── FileStoreFactory::make() ──────────────────────────────────────────────────

fixpp::core::expected_t<std::unique_ptr<MessageStore>> FileStoreFactory::make(
    std::string_view sender, std::string_view target, std::pmr::memory_resource* mr,
    std::size_t max_store_memory_bytes, asio::any_io_executor file_io_executor) noexcept {
    // ── T030: storage-DoS construction guard (FR-014 / I-11) ─────────────────
    // Applied BEFORE CompID validation and any file ops (defence-in-depth; the
    // Config's capacity/frame constraints are validated unconditionally since
    // FileStore has a single durability domain, not a bounded/unbounded split).
    // FileStore does not have per-direction capacity — it only has max_frame_bytes.
    // Guard rules applicable to FileStore:
    //   (c) max_frame_bytes > Framer::Config::max_frame_bytes → store_factory_failed
    // Note: FileStore has no inbound/outbound_capacity bounds — those are
    // MemoryStore-only (FR-014 / I-11 focus). For FileStore the DoS vector is
    // max_frame_bytes exceeding the Framer cap (oversized-frame write capability).
    {
        // (c) max_frame_bytes must not exceed the Framer's maximum frame size
        if (cfg_.max_frame_bytes > fixpp::wire::default_max_frame_bytes) {
            return std::unexpected(fixpp::core::error::store_factory_failed);
        }
        // For FileStore there is no per-session count cap (frames are disk-bound);
        // the max_store_memory_bytes cap is not applicable to FileStore's on-disk
        // growth model. We do NOT apply (a)/(b) here — FileStore is disk-bound,
        // not memory-bound. The sentinel check serves as the integrity gate.
        (void)max_store_memory_bytes;  // accepted; not applicable to FileStore disk model
    }

    // ── (1) CompID filesystem-safety validation (BEFORE any file op) ──────────
    // Per [2e §D.4] Gap 1 close. Uses primitive string_view operations only (N-5).

    const std::string dir_native = cfg_.directory.native();
    const std::string_view dir_sv{dir_native};

    // Validate sender
    if (auto err = detail::validate_compid_filesystem_safety(sender, dir_sv, target)) {
        return std::unexpected(*err);
    }
    // Validate target
    if (auto err = detail::validate_compid_filesystem_safety(target, dir_sv, sender)) {
        return std::unexpected(*err);
    }

    // ── (2) Resolve file_io_executor (Config-supplied-wins) ───────────────────
    // Config non-empty → use it; else threaded-in 5th-param; both empty → fail.
    asio::any_io_executor resolved_executor;
    if (cfg_.file_io_executor) {
        resolved_executor = cfg_.file_io_executor;
    } else if (file_io_executor) {
        resolved_executor = std::move(file_io_executor);
    } else {
        return std::unexpected(fixpp::core::error::store_factory_failed);
    }

    // ── Build the log file path and create the directory if needed ────────────
    // NOW safe to use std::filesystem (validation has passed — N-5 respected).
    std::string log_filename;
    log_filename.reserve(sender.size() + 2 + target.size() + 4);
    log_filename.append(sender);
    log_filename.append("__");
    log_filename.append(target);
    log_filename.append(".log");

    std::filesystem::path log_path;
    {
        // Create the directory if it doesn't exist.
        // Note: We do minimal error handling here — if directory creation fails
        // the subsequent file open will also fail and we'll catch it there.
        std::error_code ec;
        std::filesystem::create_directories(cfg_.directory, ec);
        log_path = cfg_.directory / log_filename;
    }

    // ── Construct resolved Config ─────────────────────────────────────────────
    FileStore::Config resolved_cfg = cfg_;
    resolved_cfg.sender_comp_id = std::string(sender);
    resolved_cfg.target_comp_id = std::string(target);
    resolved_cfg.file_io_executor = resolved_executor;
    if (resolved_cfg.store_resource == nullptr && mr != nullptr) {
        resolved_cfg.store_resource = mr;
    }

    // ── (3) Construct FileStore — the ctor is where we open + lock + scan ─────
    // [2e §4.3.2]:665 required-at-construction: FileStore is constructed here
    // inside make() once the executor is resolved, preserving the contract.
    auto store = std::make_unique<FileStore>(std::move(resolved_cfg));

    // Open the log file: takes advisory lock + runs restart scan.
    // open_log() is engine-internal (documented; not a friend, per plan.md §88).
    const std::string log_path_str = log_path.native();
    if (!store->open_log(log_path_str)) {
        return std::unexpected(fixpp::core::error::store_factory_failed);
    }

    return fixpp::core::expected_t<std::unique_ptr<MessageStore>>{std::move(store)};
}

}  // namespace fixpp::session
