// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_fail_reconcile_readfail.cpp
//
// 059-outbound-store-fail-closed — /speckit-verify coverage witness (Arm b
// else-arm + Arm a store_cancelled false-leg), per
// .specify/decisions/059-outbound-store-fail-closed-coverage-design.md.
//
// W1/W3/breadth all drive a store() failure where the subsequent reconcile
// read next_seqnum(outbound,false) SUCCEEDS — so two branches in
// store_then_emit's fatal disposition (src/session/session.cpp ~:4804-4823)
// stay uncovered by those witnesses:
//
//   Arm (b) else-arm  — the reconcile read FAILS (dk.has_value()==false):
//       auto dk = co_await store_->next_seqnum(direction_t::outbound, false);
//       if (dk.has_value()) { ...set_next_outbound... }   // <-- false leg
//     The FileStore pwrite seam CANNOT reach this: FileStore serves next_seqnum
//     from its still-valid in-memory counter after a pwrite failure. A store
//     DOUBLE is the right vehicle (coverage-design record Arm b). This witness
//     proves a reconcile-read failure is best-effort (D4): it neither prevents
//     the fail-closed co_return unexpected(err) nor mutates the returned error
//     (which remains the ORIGINAL store() error captured before reconcile,
//     D2/NEW-P3).
//
//   Arm (a) store_cancelled false-leg — the `!= store_cancelled` guard (:4805)
//     evaluates FALSE: a persistent store returning store_cancelled is
//     cancellation-class (D7), excluded from the fatal branch → logged-then-
//     proceed, unchanged (send SUCCEEDS, session stays Active, frame IS
//     transmitted). The breadth witness covers the TRUE leg (genuine failures);
//     this covers the FALSE leg. (Dissolves the coverage-design record's
//     ex-ante Arm (a) risk-assessment into a real witness now that the double
//     exists.)
//
// The reconcile `catch (...)` arm (:4819) is NOT witnessed here and is waived
// in the verify record as a cancellation-race branch (Article IX §1, no stable
// stimulus). It IS production-reachable: a real store (FileStore) serves
// next_seqnum by acquiring the async_mutex → suspends → is cancellable, and a
// co_await of a cancelled store method throws asio::system_error{
// operation_aborted} in the AWAITER's frame even though the store method is
// `noexcept` — the exact mechanism the OUTER catch at session.cpp:4827 absorbs
// from `co_await store_->store()` (see its comment at :4790-4793). This
// store-double cannot reach :4819 because ReconcileFaultStore::next_seqnum is
// synchronous (co_return cur; — no suspension, nothing to cancel), and driving
// the cancellation race deterministically is inherently flaky — the same
// no-stable-stimulus rationale the coverage-design record used for Arm (a)'s
// store_cancelled-on-shutdown-drain. (An earlier draft wrongly waived this as
// "noexcept → std::terminate → unreachable"; that is disproved by :4827.)
//
// Harness mirrors test_store_fail_reconcile_breadth.cpp (single-threaded
// io_context): the reconcile-read fault is deterministic (no strand race to
// mask), and the discrimination is the disposition + coverage arms, not
// concurrency.
//
// Anchors: specs/059-outbound-store-fail-closed/{spec.md FR-002/FR-005/FR-007;
// research.md D2/D4/D7}; coverage-design record Arm (a)/(b).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>

#include "support/pump_until_ready.hpp"
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/extract_tag.hpp"

using namespace std::chrono_literals;

namespace {

using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::Session;
using fixpp::session::session_role;

// ── Frame-building helpers (mirror test_store_fail_reconcile_breadth.cpp) ────

std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

std::vector<std::byte> make_fix_frame(std::string_view begin_string, std::string_view msg_type,
                                      std::uint32_t seq, std::string_view sender,
                                      std::string_view target, std::string_view extra = {}) {
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
    if (!extra.empty()) body += std::string(extra);

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                  std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

std::vector<std::byte> make_app_payload(std::string_view clordid) {
    std::string body = "35=D\x01" + std::string(field(11, clordid)) +
                       "54=1\x01"
                       "55=AAPL\x01";
    std::vector<std::byte> v;
    v.reserve(body.size());
    for (char c : body) v.push_back(static_cast<std::byte>(c));
    return v;
}

using fixpp::test_support::extract_tag;

// ── ReconcileFaultStore: a persistent store double that can ALSO fail the
//    reconcile read ───────────────────────────────────────────────────────
//
// Extends the breadth test's ArmableFaultStore concept: store() fails once at
// fail_at_outbound_seq_ with store_err_, AND next_seqnum(outbound,false) fails
// once — but ONLY after store() has already fired (store_fired_) and ONLY on
// the reconcile signature (increment==false). That gating is precise:
//   * persist_outbound_advance_ (session.cpp:720) reads next_seqnum(outbound,
//     increment=TRUE) — never faulted (wrong increment flag).
//   * the reconcile read (session.cpp:4815) is next_seqnum(outbound,
//     increment=FALSE), the first such read after store_fired_ → faulted once.
// yields_persistent_store() defaults to true (no factory override), so this
// store is classified persistent (research.md D6 fallback), same as the
// breadth double.
class ReconcileFaultStore final : public MessageStore {
public:
    ReconcileFaultStore(fixpp::core::error store_err, seqnum_t fail_at_outbound_seq,
                        bool fail_reconcile_read)
        : MessageStore(flush_thunk_for<ReconcileFaultStore>()),
          store_err_(store_err),
          fail_at_(fail_at_outbound_seq),
          fail_reconcile_read_(fail_reconcile_read) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> /*frame*/, direction_t dir) noexcept override {
        if (dir == direction_t::outbound && seq == fail_at_ && !store_fired_) {
            store_fired_ = true;
            co_return std::unexpected(store_err_);
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t /*begin*/, seqnum_t /*end*/, direction_t /*dir*/,
        retrieve_visitor& /*visitor*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        // Fault ONLY the reconcile read: outbound, read-only (increment==false),
        // after store() has failed, exactly once.
        if (dir == direction_t::outbound && !increment && store_fired_ && fail_reconcile_read_ &&
            !read_fired_) {
            read_fired_ = true;
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        auto& c = (dir == direction_t::outbound) ? next_out_ : next_in_;
        const seqnum_t cur = c;
        if (increment) ++c;
        co_return cur;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_in_ = next_out_ = 1;
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] bool store_fired() const noexcept { return store_fired_; }
    [[nodiscard]] bool read_fired() const noexcept { return read_fired_; }

private:
    fixpp::core::error store_err_;
    seqnum_t fail_at_;
    bool fail_reconcile_read_;
    bool store_fired_ = false;
    bool read_fired_ = false;
    seqnum_t next_in_ = 1;
    seqnum_t next_out_ = 1;
};

class ReconcileFaultStoreFactory final : public MessageStoreFactory {
public:
    ReconcileFaultStoreFactory(fixpp::core::error store_err, seqnum_t fail_at_outbound_seq,
                               bool fail_reconcile_read)
        : store_err_(store_err),
          fail_at_(fail_at_outbound_seq),
          fail_reconcile_read_(fail_reconcile_read) {}

    mutable ReconcileFaultStore* last_store = nullptr;

    // No yields_persistent_store() override — defaults to true (persistent).

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto s = std::make_unique<ReconcileFaultStore>(store_err_, fail_at_, fail_reconcile_read_);
        last_store = s.get();
        return s;
    }

private:
    fixpp::core::error store_err_;
    seqnum_t fail_at_;
    bool fail_reconcile_read_;
};

// ── Fixture ─────────────────────────────────────────────────────────────────

class StoreFailReconcileReadFailTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Drives an initiator Session to Active over the fault store, then sends the
    // app frame at seq 2 (the store-fail target). Returns (send result, wire).
    struct Driven {
        fixpp::core::expected_t<void> send_r;
        std::vector<std::vector<std::byte>> wire;
        fsm_state state;
        // Captured from the store double BEFORE ~Session frees it (the store is
        // owned by the Session; reading factory->last_store after drive() returns
        // is a use-after-free).
        bool store_fired = false;
        bool read_fired = false;
    };

    // ⚠️ EVERY MISS BRANCH BELOW POISONS `send_r`, AND IT IS UNOBSERVABLE TODAY. Both
    // halves matter, and the first draft of this comment claimed only the first.
    //
    // WHY IT IS THERE: a default-constructed `expected_t<void>` HAS a value, so an early
    // return that left the field alone hands back a struct that says SUCCESS.
    //
    // ⚠️ WHY NO TEST CAN SEE THAT TODAY, measured rather than assumed: every caller runs
    // `ASSERT_TRUE(d.store_fired)` BEFORE it reads `send_r`, and `store_fired` is false on
    // every miss branch -- so `ASSERT_` returns from the test body first and `send_r` is
    // never read. Forcing this site with and without the poison produces IDENTICAL output.
    // It is kept because it costs one line and the ordering above is a property of
    // today's two callers, not of `drive()`; it is NOT kept because anything proves it.
    // Do not write that an arm demonstrates this poison -- no arm here can.
    Driven drive(std::shared_ptr<ReconcileFaultStoreFactory> factory) {
        fixpp::session::SessionConfig cfg;
        cfg.role = session_role::initiator;
        cfg.sender_comp_id = "INITR";
        cfg.target_comp_id = "ACCEPTR";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.store_factory = factory;

        Driven out;
        cfg.transport_send = [&](std::span<const std::byte> f) {
            out.wire.emplace_back(f.begin(), f.end());
        };

        auto sess = std::make_unique<Session>(engine, cfg);

        auto open_r = asio::co_spawn(ioc, sess->open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, open_r, 200ms,
                                                        "ReconcileFixture::drive/open")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "ReconcileFixture::drive/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ReconcileFixture::drive/open";
            // The miss branch must POISON `send_r`: a default-constructed
            // expected_t<void> HAS a value, so an early return that left it alone
            // would report success to every caller.
            out.send_r = std::unexpected(fixpp::test_support::kWindowMissSentinel);
            out.state = sess->state();
            return out;
        }
        EXPECT_TRUE(open_r.get().has_value()) << "open() must succeed";
        EXPECT_EQ(sess->state(), fsm_state::LogonSent);

        auto peer_logon = make_logon("FIX.4.2", 1, "ACCEPTR", "INITR");
        auto logon_r = asio::co_spawn(
            ioc, sess->on_inbound_frame(std::span<const std::byte>(peer_logon)), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, logon_r, 200ms,
                                                        "ReconcileFixture::drive/logon")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "ReconcileFixture::drive/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ReconcileFixture::drive/logon";
            // Poison `send_r` -- see the note above `drive()`.
            out.send_r = std::unexpected(fixpp::test_support::kWindowMissSentinel);
            out.state = sess->state();
            return out;
        }
        EXPECT_TRUE(logon_r.get().has_value()) << "peer Logon-ack must be accepted";
        EXPECT_EQ(sess->state(), fsm_state::Active);

        auto payload = make_app_payload("ORD-K");
        auto send_fut =
            asio::co_spawn(ioc, sess->send(std::span<const std::byte>(payload)), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, send_fut, 200ms,
                                                        "ReconcileFixture::drive/send")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "ReconcileFixture::drive/send");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ReconcileFixture::drive/send";
            // Poison `send_r` -- see the note above `drive()`.
            out.send_r = std::unexpected(fixpp::test_support::kWindowMissSentinel);
            out.state = sess->state();
            return out;
        }
        out.send_r = send_fut.get();
        out.state = sess->state();

        // Capture the store-double flags while the store is still alive (owned
        // by sess, freed at ~Session below).
        EXPECT_NE(factory->last_store, nullptr) << "the factory must have minted a store";
        if (factory->last_store != nullptr) {
            out.store_fired = factory->last_store->store_fired();
            out.read_fired = factory->last_store->read_fired();
        }

        // Terminal close (drain the async_mutex before ~Session).
        asio::co_spawn(ioc, sess->close(fixpp::session::close_mode::terminal), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────
// Arm (b) else-arm — the reconcile read fails; still fail-closed, error
// unchanged (the ORIGINAL store() error), failing frame not transmitted.
// ─────────────────────────────────────────────────────────────────────────
TEST_F(StoreFailReconcileReadFailTest, ReconcileReadFails_StillFailsClosed_ErrorUnchanged) {
    // store() fails at seq 2 with store_capacity_exhausted; the subsequent
    // reconcile read next_seqnum(outbound,false) ALSO fails (store_io_failure).
    // The two error codes are DISTINCT so the assertion below discriminates:
    // the returned error must be the store() error, NOT the reconcile-read error.
    constexpr seqnum_t kFailAtSeq = 2;
    auto factory = std::make_shared<ReconcileFaultStoreFactory>(
        fixpp::core::error::store_capacity_exhausted, kFailAtSeq, /*fail_reconcile_read=*/true);

    auto d = drive(factory);

    ASSERT_TRUE(d.store_fired) << "the store() fault must have fired for seq " << kFailAtSeq;
    ASSERT_TRUE(d.read_fired)
        << "the reconcile-read fault (else-arm stimulus) must have fired — otherwise this "
           "witness proves nothing about the dk.has_value()==false branch "
           "(feedback_strand_local_drain_witness_stimulus_must_reach_codepath)";

    // Fail-closed disposition survives a reconcile-read failure (D4 best-effort).
    EXPECT_FALSE(d.send_r.has_value())
        << "a persistent-store retain failure must fail closed even when the reconcile "
           "read also fails (reconcile is best-effort, not a gate on fail-closed)";
    EXPECT_EQ(d.send_r.error(), fixpp::core::error::store_capacity_exhausted)
        << "the returned error must remain the ORIGINAL store() error (captured before "
           "reconcile, D2/NEW-P3) — a reconcile-read failure (store_io_failure) must NOT "
           "leak into the propagated error code";
    EXPECT_EQ(d.state, fsm_state::Disconnected)
        << "the store-fatal class must transition to Disconnected (T007 widened guard)";

    bool failing_frame_transmitted = false;
    for (const auto& frame : d.wire) {
        if (extract_tag(frame, 34) == std::to_string(kFailAtSeq)) failing_frame_transmitted = true;
    }
    EXPECT_FALSE(failing_frame_transmitted)
        << "FR-002: the un-retained frame (seq=" << kFailAtSeq << ") must NOT be transmitted";
}

// ─────────────────────────────────────────────────────────────────────────
// Arm (a) store_cancelled false-leg — a persistent store returning
// store_cancelled is cancellation-class (D7), excluded from the fatal branch:
// logged-then-proceed, send SUCCEEDS, session stays Active, frame transmitted.
// ─────────────────────────────────────────────────────────────────────────
TEST_F(StoreFailReconcileReadFailTest, StoreCancelled_NotFatal_StaysActiveAndTransmits) {
    constexpr seqnum_t kFailAtSeq = 2;
    auto factory = std::make_shared<ReconcileFaultStoreFactory>(
        fixpp::core::error::store_cancelled, kFailAtSeq, /*fail_reconcile_read=*/false);

    auto d = drive(factory);

    ASSERT_TRUE(d.store_fired) << "the store() fault must have fired with store_cancelled for seq "
                               << kFailAtSeq;

    // store_cancelled is excluded (`!= store_cancelled` FALSE) → unchanged path.
    EXPECT_TRUE(d.send_r.has_value())
        << "store_cancelled is cancellation-class (D7): the fatal branch is skipped, so "
           "send() must NOT fail closed (logged-then-proceed, unchanged)";
    EXPECT_EQ(d.state, fsm_state::Active)
        << "store_cancelled must leave the session Active (not the store-fatal transition)";

    bool frame_transmitted = false;
    for (const auto& frame : d.wire) {
        if (extract_tag(frame, 34) == std::to_string(kFailAtSeq)) frame_transmitted = true;
    }
    EXPECT_TRUE(frame_transmitted)
        << "store_cancelled falls through to Step 2: the frame (seq=" << kFailAtSeq
        << ") IS transmitted, exactly as today (FR-005 cancellation parity)";
}

}  // namespace
