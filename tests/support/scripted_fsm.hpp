// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/scripted_fsm.hpp
//
// 007-threading-clock — DETERMINISTIC SCRIPTED TEST-DOUBLE FSM (D-5 /
// Clarifications 2026-05-19 Q2). The real FIX session FSM is the deferred
// 005's; the FSM-shaped [2d §9] seams (3 executor-compat, 9 heartbeat-window,
// 15 third-party-Clock conformance / SC-006, 16 direct_executor reentrancy)
// and the seam-11 clock-injection corpus are driven by THIS scripted player.
//
// SCOPE GUARD (carry into every consuming seam): this drives a FIXED,
// deterministic label sequence and asserts ONLY 2d-owned properties —
//   • strand serialisation (no two callbacks overlap within a session)
//   • mock_clock determinism (identical advance ⇒ identical observation)
//   • two-phase cancellation (graceful child-state vs terminal root-total)
//   • alloc / latency
// It NEVER asserts FIX FSM correctness (Logon/gap-fill/ResendRequest/
// sequence-reset — that is 005's). The FIX message names below are SCRIPT
// LABELS, not real protocol output. The fixture picks values 2d does not
// pin (e.g. heartbeat interval).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace fixpp::testsupport {

// Script labels — the canonical Logon→NewOrderSingle→ExecutionReport→Logout
// sequence plus heartbeat windows and the two-phase-close trigger. These are
// test-double script labels, NOT real FIX FSM transitions (D-5).
enum class fsm_label : std::uint8_t {
    logon = 0,
    new_order_single,
    execution_report,
    heartbeat,
    logout,
    advance_clock,  // step the injected Clock by `delta`
    request_close,  // trigger Session::close(close_mode)
};

struct fsm_step {
    fsm_label label;
    std::chrono::nanoseconds delta{0};  // used only by advance_clock
    bool terminal{false};               // used only by request_close
};

using fsm_script = std::vector<fsm_step>;

// The default 2d-scoped sequence (seams 3/15 + the clock-injection corpus).
// Deterministic and stable across runs — the seam-11 golden diff depends on
// byte-stable ordering.
[[nodiscard]] inline fsm_script make_default_script() {
    return fsm_script{
        {fsm_label::logon},
        {fsm_label::new_order_single},
        {fsm_label::execution_report},
        {fsm_label::logout},
    };
}

// A heartbeat-window script (seam 9): N heartbeat cycles each preceded by a
// deterministic clock advance. `interval` is a value 2d does not pin —
// chosen by the fixture (D-5 / Assumptions).
[[nodiscard]] inline fsm_script make_heartbeat_script(int cycles,
                                                      std::chrono::nanoseconds interval) {
    fsm_script s;
    s.push_back({fsm_label::logon});
    for (int i = 0; i < cycles; ++i) {
        s.push_back({fsm_label::advance_clock, interval, false});
        s.push_back({fsm_label::heartbeat});
    }
    s.push_back({fsm_label::logout});
    return s;
}

// The "Logon→NewOrderSingle→cancel" third-party-Clock conformance / SC-006
// script (seam 15): cancellation is requested mid-flight via request_close.
[[nodiscard]] inline fsm_script make_conformance_cancel_script(bool terminal) {
    return fsm_script{
        {fsm_label::logon},
        {fsm_label::new_order_single},
        {fsm_label::request_close, std::chrono::nanoseconds{0}, terminal},
    };
}

// Thread-safe ordered observation log. Each callback the session dispatches
// appends an entry; the strand-serialisation seam (2) asserts no two entries
// for the same session overlap and determinism seams diff the label order.
class observation_log {
public:
    struct entry {
        fsm_label label{};
        std::uint64_t order{};  // global monotonic sequence
        std::thread::id tid;    // which thread ran the callback (default-id)
        bool reentered{};       // overlap detected (must stay false)
    };

    // Called at callback ENTRY. Returns the assigned order index. Records an
    // overlap if another callback for this log is still "inside".
    std::uint64_t on_enter(fsm_label l) {
        const bool was_inside = inside_.exchange(true, std::memory_order_acq_rel);
        const auto ord = next_.fetch_add(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> g(m_);
        entries_.push_back(entry{l, ord, std::this_thread::get_id(), was_inside});
        return ord;
    }
    void on_leave() noexcept { inside_.store(false, std::memory_order_release); }

    [[nodiscard]] std::vector<entry> snapshot() const {
        std::lock_guard<std::mutex> g(m_);
        return entries_;
    }
    // 2d-owned: strand serialisation holds iff NO entry was reentered.
    [[nodiscard]] bool strictly_serialised() const {
        std::lock_guard<std::mutex> g(m_);
        for (const auto& e : entries_) {
            if (e.reentered) return false;
        }
        return true;
    }
    // determinism: the label order is stable across identically-driven runs.
    [[nodiscard]] std::vector<fsm_label> label_order() const {
        std::lock_guard<std::mutex> g(m_);
        std::vector<fsm_label> v;
        v.reserve(entries_.size());
        for (const auto& e : entries_) {
            v.push_back(e.label);
        }
        return v;
    }

private:
    mutable std::mutex m_;
    std::vector<entry> entries_;
    std::atomic<bool> inside_{false};
    std::atomic<std::uint64_t> next_{0};
};

// RAII span: records on_enter at construction, on_leave at destruction —
// wrapped around each dispatched callback by the consuming seam so the
// overlap/serialisation property is observed without a real FIX FSM.
class observed_callback {
public:
    observed_callback(observation_log& log, fsm_label l) : log_(log) { log_.on_enter(l); }
    ~observed_callback() { log_.on_leave(); }
    observed_callback(const observed_callback&) = delete;
    observed_callback& operator=(const observed_callback&) = delete;
    observed_callback(observed_callback&&) = delete;
    observed_callback& operator=(observed_callback&&) = delete;

private:
    observation_log& log_;
};

}  // namespace fixpp::testsupport
