// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/quickfix_compat/cfg_loader.cpp
//
// fixpp::session::quickfix_compat::cfg_to_file_store_factory implementation.
//
// Anchor: .specify/2e-msgstore.md v0.4 §4.8.A.2 / §4.8.A.3. FR-030.
// Entity E12.  Task T047.
//
// CONFIG TRANSLATION ONLY — NO runtime adapter (Path A retired v0.3 per
// Codex C-R2-P2-1; [2e §4.8.B]).
//
// Parsing discipline:
//   - Windows-INI-style .cfg: [DEFAULT] and [SESSION] sections.
//   - Keys extracted: FileStorePath, SenderCompID, TargetCompID.
//   - All other keys are ignored (forward-compatible; future keys may appear).
//   - [SESSION] values override [DEFAULT] values per QuickFIX semantics.
//   - Blank lines and comment lines (# or ;) are skipped.
//   - Key=Value pairs: leading/trailing whitespace on key and value is stripped.
//
// noexcept discipline:
//   - std::ifstream construction can throw on some implementations; we use the
//     is_open() check pattern exclusively.  All std::filesystem calls and
//     std::string operations that might throw are wrapped in try/catch.
//   - The outer boundary is a catch(...) that converts any thrown exception to
//     store_factory_failed.

#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/detail/validate_compid_filesystem_safety.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/quickfix_compat/cfg_loader.hpp>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace fixpp::session::quickfix_compat {

namespace {

// Trim leading and trailing ASCII whitespace (space, tab, carriage-return).
[[nodiscard]] std::string_view trim(std::string_view sv) noexcept {
    constexpr std::string_view ws = " \t\r";
    const auto first = sv.find_first_not_of(ws);
    if (first == std::string_view::npos) return {};
    const auto last = sv.find_last_not_of(ws);
    return sv.substr(first, last - first + 1);
}

// Section tags we care about.
enum class Section { none, default_section, session_section, other };

[[nodiscard]] Section parse_section_tag(std::string_view line) noexcept {
    // line has been trimmed and is known to start with '['.
    // Accepted: [DEFAULT], [SESSION] (case-insensitive as QuickFIX is).
    // We do simple ASCII case-fold for the two known tags.
    if (line.size() < 2 || line.back() != ']') return Section::other;
    std::string_view tag = line.substr(1, line.size() - 2);
    // Fast path exact match (most configs use uppercase).
    if (tag == "DEFAULT") return Section::default_section;
    if (tag == "SESSION") return Section::session_section;
    // Case-insensitive fallback for non-uppercase configs.
    if (tag.size() == 7) {
        char buf[8];
        for (std::size_t i = 0; i < 7; ++i) {
            char c = tag[i];
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
            buf[i] = c;
        }
        buf[7] = '\0';
        std::string_view upper(buf, 7);
        if (upper == "DEFAULT") return Section::default_section;
        if (upper == "SESSION") return Section::session_section;
    }
    return Section::other;
}

// Holds the three keys we care about from the merged [DEFAULT]+[SESSION] pass.
struct ParsedKeys {
    std::string file_store_path;
    std::string sender_comp_id;
    std::string target_comp_id;
};

// Returns false on I/O failure; sets out_keys on success (keys may be empty
// if not present in the file).
[[nodiscard]] bool parse_cfg(const std::filesystem::path& cfg_path, ParsedKeys& out_keys) noexcept {
    // We'll do a single pass, collecting [DEFAULT] values first, then letting
    // [SESSION] values override.  The merge is done via two ParsedKeys structs.
    ParsedKeys def{};
    ParsedKeys ses{};

    std::ifstream ifs;
    // std::ifstream constructor may throw; use try/catch.
    try {
        ifs.open(cfg_path);
    } catch (...) {
        return false;
    }
    if (!ifs.is_open()) return false;

    Section current = Section::none;
    std::string line;

    try {
        while (std::getline(ifs, line)) {
            std::string_view sv = trim(std::string_view(line));

            // Skip blank lines and comments.
            if (sv.empty() || sv[0] == '#' || sv[0] == ';') continue;

            // Section header.
            if (sv[0] == '[') {
                current = parse_section_tag(sv);
                continue;
            }

            // Only process DEFAULT and SESSION sections.
            if (current != Section::default_section && current != Section::session_section) {
                continue;
            }

            // Key=Value pair.
            const auto eq = sv.find('=');
            if (eq == std::string_view::npos) continue;

            std::string_view key = trim(sv.substr(0, eq));
            std::string_view value = trim(sv.substr(eq + 1));

            // Select target struct.
            ParsedKeys& target = (current == Section::session_section) ? ses : def;

            if (key == "FileStorePath") {
                target.file_store_path = std::string(value);
                continue;
            }
            if (key == "SenderCompID") {
                target.sender_comp_id = std::string(value);
                continue;
            }
            if (key == "TargetCompID") {
                target.target_comp_id = std::string(value);
                continue;
            }
            // All other keys ignored.
        }
    } catch (...) {
        return false;
    }

    // Merge: SESSION overrides DEFAULT per QuickFIX semantics.
    // (empty session value means "not present in SESSION" — keep DEFAULT).
    out_keys.file_store_path =
        ses.file_store_path.empty() ? def.file_store_path : ses.file_store_path;
    out_keys.sender_comp_id = ses.sender_comp_id.empty() ? def.sender_comp_id : ses.sender_comp_id;
    out_keys.target_comp_id = ses.target_comp_id.empty() ? def.target_comp_id : ses.target_comp_id;

    return true;
}

}  // namespace

// ── cfg_to_file_store_factory ─────────────────────────────────────────────────

[[nodiscard]] fixpp::core::expected_t<std::unique_ptr<FileStoreFactory>> cfg_to_file_store_factory(
    const std::filesystem::path& cfg_path) noexcept {
    // Outer exception barrier — any unexpected throw maps to store_factory_failed.
    try {
        // ── (1) Parse the .cfg file ───────────────────────────────────────────
        ParsedKeys keys{};
        if (!parse_cfg(cfg_path, keys)) {
            return std::unexpected(fixpp::core::error::store_factory_failed);
        }

        // ── (2) Require all three keys ────────────────────────────────────────
        if (keys.file_store_path.empty() || keys.sender_comp_id.empty() ||
            keys.target_comp_id.empty()) {
            return std::unexpected(fixpp::core::error::store_factory_failed);
        }

        // ── (3) CompID filesystem-safety validation (defense-in-depth) ────────
        // Mirrors the same rule set as FileStoreFactory::make() step (1) per
        // [2e §D.4] Gap 1 close.  Validates at config-load time so malformed
        // CFGs fail fast before any session thread is involved.
        const std::string dir_native = keys.file_store_path;
        const std::string_view dir_sv{dir_native};

        // Validate sender.
        if (auto err = fixpp::session::detail::validate_compid_filesystem_safety(
                keys.sender_comp_id, dir_sv, keys.target_comp_id)) {
            return std::unexpected(*err);
        }
        // Validate target.
        if (auto err = fixpp::session::detail::validate_compid_filesystem_safety(
                keys.target_comp_id, dir_sv, keys.sender_comp_id)) {
            return std::unexpected(*err);
        }

        // ── (4) Build FileStore::Config ───────────────────────────────────────
        // file_io_executor is left default-constructed (empty).
        // The engine populates it from EngineConfig::file_io_executor at
        // FileStoreFactory::make() time per [2e §4.3.2]:665.
        FileStore::Config cfg;
        cfg.directory = std::filesystem::path(keys.file_store_path);
        cfg.sender_comp_id = keys.sender_comp_id;
        cfg.target_comp_id = keys.target_comp_id;
        // policy, max_frame_bytes, store_resource — use defaults from Config.
        // file_io_executor — intentionally left empty (engine fills it at make()).

        // ── (5) Emit the factory ──────────────────────────────────────────────
        return std::make_unique<FileStoreFactory>(std::move(cfg));

    } catch (...) {
        return std::unexpected(fixpp::core::error::store_factory_failed);
    }
}

}  // namespace fixpp::session::quickfix_compat
