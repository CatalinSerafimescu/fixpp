// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_session_layering.cpp
//
// 005-session-establishment-fsm T060 / Phase 8 — noexcept window + the
// no-C++-across-C-ABI layering check (FR-015 / SC-009).
//
// Three properties verified:
//
//  1. Compile-time `noexcept` attestation on every public Session surface
//     across the inbound-process / timer-fire window (the surfaces enumerated
//     in plan.md "Constraints" §1 and contracts/session.hpp). Per FR-015,
//     these must propagate `noexcept` so the throwing-user-callback policy can
//     hold structurally rather than as a runtime guard.
//
//  2. The compiled `libfixpp_session.a` archive exports zero `extern "C"`
//     symbols matching the `fixpp_session_*` prefix — the C-ABI surface is
//     `2i`-owned; this PR adds NONE per [const §X.2] / FR-015 / plan.md
//     Constitution Check row §X.2 (`nm` check).
//
//  3. The public C-ABI header `<fix/c_api.h>` (and its umbrella subtree
//     `<fix/c_api/*>`) contains zero references to any `fixpp::session::`
//     type — no C++ leakage through the C ABI ([const §X.2]).
//
// (4) The "throwing-user-callback trap" sibling of FR-015 (`core::detail::
//     trap_throw` wrap of fromAdmin / fromApp / transport_send invocations)
//     is OUT OF SCOPE here. 005 does not yet wire user `fromAdmin` / `fromApp`
//     callbacks (no field exists on `SessionConfig`), and the `transport_send`
//     invocation site in `src/session/session.cpp` is not yet wrapped. Surface
//     and resolve that gap via /speckit-verify against FR-015 — it is not a
//     T060 assertion.

#include <gtest/gtest.h>
#ifndef _WIN32
#include <unistd.h>  // mkstemp/close — only the POSIX nm sub-test uses these
#endif

#include <asio/awaitable.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fstream>
#include <span>
#include <string>
#include <type_traits>

namespace {

// ── (1) Compile-time `noexcept` attestation ─────────────────────────────────
//
// Each static_assert is a binary property: the build IS the test. A drift in
// the impl that removes `noexcept` from a covered surface fails this TU's
// compile, surfacing the FR-015 violation at build time rather than runtime.

using ::fixpp::session::seqnum_t;
using ::fixpp::session::SeqnumManager;
using ::fixpp::session::Session;

// Session::on_inbound_frame — inbound-process window (FR-015).
static_assert(noexcept(std::declval<Session&>().on_inbound_frame(std::span<const std::byte>{})),
              "Session::on_inbound_frame must be noexcept (FR-015 inbound window)");

// Session::send — outbound durable-before-transmit path (I-3 / FR-015).
static_assert(noexcept(std::declval<Session&>().send(std::span<const std::byte>{})),
              "Session::send must be noexcept (FR-015 outbound window)");

// Session::state — single-writer FSM read (FR-016 / FR-015).
static_assert(noexcept(std::declval<const Session&>().state()),
              "Session::state must be noexcept (FR-015 / FR-016)");

// Session::open — admin lifecycle; declared noexcept per plan.md `Constraints`.
static_assert(noexcept(std::declval<Session&>().open()), "Session::open must be noexcept (FR-015)");

// Session::session_arena — engine-internal accessor (I-18).
static_assert(noexcept(std::declval<const Session&>().session_arena()),
              "Session::session_arena must be noexcept");

// Session::root_cancellation_slot — engine-internal accessor (T039 / I-07).
static_assert(noexcept(std::declval<Session&>().root_cancellation_slot()),
              "Session::root_cancellation_slot must be noexcept");

// SeqnumManager::check_inbound / assign_outbound — timer-fire-adjacent paths
// (the SeqnumManager is the counter back-end for the inbound advance / outbound
// stamp; FR-015 noexcept window covers it).
static_assert(noexcept(std::declval<SeqnumManager&>().check_inbound(seqnum_t{1})),
              "SeqnumManager::check_inbound must be noexcept (FR-015)");

static_assert(noexcept(std::declval<SeqnumManager&>().assign_outbound()),
              "SeqnumManager::assign_outbound must be noexcept (FR-015)");

// NOTE — `Session::close()` is intentionally NOT noexcept per the gate-b/r1
// RC#2/P2.1 carve-out documented at include/fixpp/session/session.hpp:115-121
// (first-close path allocates via std::make_shared on the cold path). It is
// outside the inbound-process / timer-fire window so FR-015 does not bind it.

// ── (2) `nm` C-ABI symbol check ─────────────────────────────────────────────
//
// Locates the compiled `libfixpp_session.a` via the CMake binary dir embedded
// at compile time, then asks `nm --extern-only --defined-only` for every
// external symbol and asserts no symbol matches the `fixpp_session_` prefix.
// The C-ABI coalescing target is 2i-owned per [const §X.2] / FR-015.

#ifndef FIXPP_TEST_BINARY_DIR
#error "FIXPP_TEST_BINARY_DIR must be defined (set by tests/session/CMakeLists.txt)"
#endif

// WAIVED on Windows: this gate shells out to `nm` (GNU binutils) over the static
// archive and matches the ELF symbol-table format. MSVC ships a different archive
// format and decorates symbols differently; the C-ABI-symbol property (005 adds
// ZERO extern-C fixpp_session_* symbols) is a Linux-artifact ABI check already
// gated in Linux CI. Cross-platform sign-off: 2026-06-22.
#ifndef _WIN32
TEST(SessionLayering, NoExternCSessionSymbolsInArchive) {
    const std::string lib_path = std::string{FIXPP_TEST_BINARY_DIR} + "/lib/libfixpp_session.a";

    // Use mkstemp for a unique temp output (avoids tmpnam deprecation warning
    // and concurrent-CTest collisions).
    char tmp_template[] = "/tmp/session_layering_nm.XXXXXX";
    const int tmp_fd = ::mkstemp(tmp_template);
    ASSERT_GE(tmp_fd, 0) << "mkstemp failed";
    ::close(tmp_fd);  // close the fd; we reuse the path via shell redirection.
    const std::string tmp{tmp_template};
    const std::string cmd = "nm --extern-only --defined-only '" + lib_path +
                            "' "
                            "| grep -E '^[0-9a-f]+\\s+[A-Z]\\s+fixpp_session_' "
                            "> '" +
                            tmp +
                            "' 2>&1; "
                            "wc -l < '" +
                            tmp + "'";

    FILE* pipe = ::popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr) << "popen(nm) failed";
    int matches = -1;
    (void)std::fscanf(pipe, "%d", &matches);
    const int rc = ::pclose(pipe);
    (void)std::remove(tmp.c_str());

    ASSERT_EQ(WEXITSTATUS(rc), 0) << "nm pipeline exited non-zero";
    EXPECT_EQ(matches, 0) << "libfixpp_session.a exports " << matches << " extern-C "
                          << "fixpp_session_* symbol(s); 005 must add ZERO C-ABI symbols "
                          << "([const §X.2] / FR-015) — those belong to 2i.";
}
#endif  // !_WIN32 (nm symbol gate is Linux-binutils-specific)

// ── (3) `<fix/c_api.h>` no-leakage source scan ──────────────────────────────
//
// The public C-ABI umbrella header and its subtree must contain zero
// references to any `fixpp::session::` type — no C++ leakage through the C
// ABI ([const §X.2]).

#ifndef FIXPP_TEST_SOURCE_DIR
#error "FIXPP_TEST_SOURCE_DIR must be defined (set by tests/session/CMakeLists.txt)"
#endif

TEST(SessionLayering, CApiHeadersHaveNoSessionTypeReferences) {
    namespace fs = std::filesystem;
    const fs::path inc_root = std::string{FIXPP_TEST_SOURCE_DIR} + "/include/fix";

    // Portable recursive scan (mirrors `grep -rEl 'fixpp::session' | wc -l`):
    // count FILES referencing the C++ `fixpp::session` namespace. No shell → runs
    // everywhere. The C-ABI snake_case `fixpp_session_*` handle prefix is the
    // legitimate published surface owned by the 2i C-ABI feature (049 handles.h:
    // `fixpp_session_t`); it is NOT a C++ leak and is enforced separately by the
    // nm exported-symbol gate ([const §X.2]) — so it is deliberately not matched.
    int matches = 0;
    std::string offending;
    std::error_code ec;
    if (fs::exists(inc_root, ec)) {
        for (fs::recursive_directory_iterator it{inc_root, ec}, end; it != end && !ec;
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::ifstream ifs(it->path());
            if (!ifs) continue;
            std::string line;
            bool hit = false;
            while (std::getline(ifs, line)) {
                if (line.find("fixpp::session") != std::string::npos) {
                    hit = true;
                    break;
                }
            }
            if (hit) {
                ++matches;
                offending += "  " + it->path().string() + "\n";
            }
        }
    }

    EXPECT_EQ(matches, 0) << "<fix/c_api.h> subtree contains " << matches
                          << " file(s) referencing the C++ fixpp::session namespace — "
                          << "no C++ leakage through the C ABI ([const §X.2] / FR-015).\n"
                          << offending;
}

}  // namespace
