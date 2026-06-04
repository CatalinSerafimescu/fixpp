// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/engine_config.hpp
//
// fixpp::core::EngineConfig — value-typed engine-level shared resources.
// Engine::open(EngineConfig) consumes it once. clock == nullptr is rejected
// with error::clock_not_set at Engine::open REGARDLESS of any session
// clock_override (root cause #2 / FR-006 / I-03; enforced in T031).
// dictionaries → dict::version_registry built at Engine::open via the
// merged-003 [2c §4.9] API (D-13; T049). engine_trace_context is published
// to consumers through engine_trace_context_snapshot, a std::atomic<…>
// snapshot with a seqlock fallback because T001 measured
// std::atomic<trace_context>::is_always_lock_free == false on every Tier-1
// STL (research D-1; [2d §4.4]). [2d §4.4]. Realizes
// specs/007-threading-clock/contracts/engine_config.hpp.
#pragma once

#include <asio/any_io_executor.hpp>
#include <atomic>
#include <cstring>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/error.hpp>  // expected_t / error (clock_not_set)
#include <fixpp/core/trace_context.hpp>
#include <fixpp/dict/version_registry.hpp>          // dict::version_registry (2d construction)
#include <fixpp/service/control_plane_factory.hpp>  // unique_ptr member ⇒ complete type
#include <memory>
#include <memory_resource>
#include <vector>

namespace fixpp::dict {
class Dictionary;
}
// Logger alias (fixpp::core::Logger = fixpp::log::Logger) — keep the heavy
// log headers off this include-edge; only the fwd-decl + alias is needed here.
// [const §XV.9] — OTel SDK / std::mutex must NOT enter via this edge.
#include <fixpp/core/logger_fwd.hpp>  // defines fixpp::core::Logger alias

namespace fixpp::otel {
class TracerProvider;
class MeterProvider;
}  // namespace fixpp::otel
namespace fixpp::session {
class Application;  // 019-app-callbacks T005 — fwd-decl to avoid pulling
                    // application.hpp (wire/parser.hpp) into this header
                    // (awaitable-corpus include-edge, [const §XV.9] /
                    // [[feedback_awaitable_header_mutex_include_edge]]).
                    // shared_ptr<incomplete type> is valid at declaration.
class MessageStoreFactory;
}  // namespace fixpp::session
namespace fixpp::tls {
class cert_source;
}
namespace fixpp::transport {
class TransportFactory;
}  // namespace fixpp::transport

namespace fixpp::core {

namespace detail {

// Engine-held publishable snapshot of fixpp::otel::trace_context.
//
// T001 (2026-05-19): std::atomic<trace_context>::is_always_lock_free == false
// on libstdc++ AND libc++ (32 B > 16 B max CAS). Per research D-1 a non-
// lock-free result MUST select an explicit seqlock fallback, never silently
// degrade to a lock-backed std::atomic. This type picks the native lock-free
// std::atomic path when (some target ever) reports is_always_lock_free, else
// a single-writer / many-reader seqlock over a memcpy of the 32-byte POD
// (correct because trace_context is trivially-copyable — pinned by the
// trace_context.hpp static_assert).
class trace_context_snapshot {
public:
    trace_context_snapshot() noexcept { store(fixpp::otel::trace_context{}); }

    explicit trace_context_snapshot(const fixpp::otel::trace_context& v) noexcept { store(v); }

    void store(const fixpp::otel::trace_context& v) noexcept {
        if constexpr (kLockFree) {
            atom_.store(v, std::memory_order_release);
        } else {
            // single-writer seqlock: odd = write in progress.
            const auto s = seq_.load(std::memory_order_relaxed);
            seq_.store(s + 1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            std::memcpy(&bytes_, &v, sizeof(v));
            std::atomic_thread_fence(std::memory_order_release);
            seq_.store(s + 2, std::memory_order_release);
        }
    }

    [[nodiscard]] fixpp::otel::trace_context load() const noexcept {
        if constexpr (kLockFree) {
            return atom_.load(std::memory_order_acquire);
        } else {
            fixpp::otel::trace_context out{};
            for (;;) {
                const auto s1 = seq_.load(std::memory_order_acquire);
                if ((s1 & 1U) != 0U) {
                    continue;
                }  // writer mid-update
                std::memcpy(&out, &bytes_, sizeof(out));
                std::atomic_thread_fence(std::memory_order_acquire);
                const auto s2 = seq_.load(std::memory_order_relaxed);
                if (s1 == s2) {
                    return out;
                }  // consistent snapshot
            }
        }
    }

    [[nodiscard]] static constexpr bool lock_free() noexcept { return kLockFree; }

private:
    static constexpr bool kLockFree = std::atomic<fixpp::otel::trace_context>::is_always_lock_free;

    // Only one of the two storage paths is ever used (selected at compile
    // time); both are declared so the type stays a single value member.
    std::atomic<fixpp::otel::trace_context> atom_;
    std::atomic<std::uint32_t> seq_{0};
    fixpp::otel::trace_context bytes_{};
};

}  // namespace detail

struct EngineConfig {
    asio::any_io_executor executor;
    std::shared_ptr<Clock> clock;  // rejected if null @ Engine::open

    std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> dictionaries;

    std::pmr::memory_resource* default_message_resource = std::pmr::get_default_resource();
    std::pmr::memory_resource* default_session_resource = std::pmr::get_default_resource();

    std::shared_ptr<fixpp::core::Logger> logger;          // null → no-op
    std::shared_ptr<fixpp::otel::TracerProvider> tracer;  // null → no-op
    std::shared_ptr<fixpp::otel::MeterProvider> meter;    // null → no-op

    std::shared_ptr<fixpp::session::MessageStoreFactory> default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source> default_cert_source;
    std::shared_ptr<fixpp::transport::TransportFactory> default_transport_factory;

    // ── 008-message-store: per-session storage-DoS cap (FR-014a) and the
    //    shared file-I/O executor for FileStore async pwrite/fdatasync
    //    (FR-024a). Both are threaded into MessageStoreFactory::make() at
    //    session-open per [2e §D.6] / FR-005 (4th + 5th parameters). Non-
    //    breaking append; do NOT renumber or relocate the fields above.
    //
    //    max_store_memory_per_session = 1 GiB default per [2e §1.2]:54;
    //      a default MemoryStore::Config (10_000-per-direction × default
    //      max_frame_bytes) intentionally exceeds this so the storage-DoS
    //      guard fires under AC US2 #2a — operators set capacities AND raise
    //      the cap deliberately, no implicit silent oversize ([const §XV.15]
    //      sibling rule).
    //
    //    file_io_executor = default-constructed empty per [2e §4.3.2]:669;
    //      MemoryStore impls silently discard it. FileStoreFactory rejects
    //      with store_factory_failed when both the Config-supplied executor
    //      AND this threaded-in value are empty (FR-024 / I-13).
    std::size_t max_store_memory_per_session = 1ULL << 30;
    asio::any_io_executor file_io_executor;

    // Seed value; the engine publishes/reads it via the atomic snapshot below
    // (the engine-fallback trace-context path — FR-015 / I-12).
    fixpp::otel::trace_context engine_trace_context{};

    std::unique_ptr<fixpp::service::ControlPlaneFactory> control_plane_factory;  // null permitted

    // ── 019-app-callbacks T005 ────────────────────────────────────────────────
    // Single per-engine Application callback receiver. null ⇒ no callbacks are
    // invoked, behaviour identical to the pre-019 engine (FR-002 / INV-1 / SC-006).
    // Lifetime: the Application MUST outlive the Engine (and every session it
    // drives); the Engine drains all in-flight callback work before destroying a
    // session ([L-015-4] / FR-012). Forward-declared above to avoid the include
    // edge from engine_config.hpp (awaitable corpus) → application.hpp →
    // wire/parser.hpp, which carries std::mutex-bearing headers ([const §XV.9] /
    // [[feedback_awaitable_header_mutex_include_edge]]). shared_ptr<incomplete>
    // is valid; the concrete type is visible at all Engine/Session call sites
    // where callbacks are actually invoked (they #include application.hpp).
    std::shared_ptr<fixpp::session::Application> application{nullptr};
};

// The Engine::open() clock_not_set precondition (FR-006 / I-03 / root cause
// #2): EngineConfig::clock == nullptr is rejected REGARDLESS of any session
// clock_override. This feature ships no full Engine type (the engine
// establishment is downstream); validate_engine_config() is the minimal
// 2d-owned realization of that engine-open invariant — the broader Engine
// calls it at open. Additional engine-open config invariants accrue here.
[[nodiscard]] inline expected_t<void> validate_engine_config(const EngineConfig& cfg) noexcept {
    if (cfg.clock == nullptr) {
        return std::unexpected(error::clock_not_set);
    }
    return expected_t<void>{};
}

// 2d-OWNED minimal realization of the "build version_registry from
// EngineConfig::dictionaries at Engine::open" step (D-13 / D-20 / T049 /
// I-15 / FR-017). This feature ships NO Engine type; the free function is
// the 2d-owned construction proxy. The downstream Engine calls it at open.
//
// Builds a dict::version_registry from cfg.dictionaries via the merged-003
// [2c §4.9] API. Non-FIXT.1.1 dicts map session_version → application_version;
// vt11 session-layer dicts are skipped (session-admin; app-version resolved
// per-message via ApplVerID(1128)).
//
// A registry built from an EMPTY dictionaries list is valid — it holds no
// entries; any subsequent get() returns dict_no_dictionary_for_application_version
// (the 2c-owned slot-28 error, NOT a 2d synonym — seam 20 / I-15).
//
// Returns expected_t<version_registry> (always succeeds for a well-formed
// EngineConfig; errors during get() are the registry's responsibility).
[[nodiscard]] inline expected_t<fixpp::dict::version_registry> build_version_registry(
    const EngineConfig& cfg) noexcept {
    return fixpp::dict::version_registry{cfg.dictionaries};
}

}  // namespace fixpp::core
