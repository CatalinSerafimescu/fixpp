// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_identity_first.cpp — 106-logon-guards-write-bounds (tasks.md T003, T038 to
// T042).
//
// The identity-first cells (research D1; research D9 rows FR-003/004, "FR-003 plain" and FR-005;
// contract C-1; data-model E-1) and the NextNumIn cells that compile on the base. Each refusal cell
// drives one Logon or acknowledgement that both triggers its refusal and fails its identity gate,
// and asserts that the arm wrote nothing, changed no number in memory or in the store, made no
// reset, and recorded Disconnected. Each Admitted cell drives the same refusal past an admitted
// identity and asserts the base's answer, numbers and ending.
//
// The rig is 099's FIXT rig (test_099_fixt_support.hpp, shape T: FIXT.1.1 on research R-18's
// registry, the validate gate on, 1137=9 the serviceable version), with a HookedStore over a
// MemoryStore that holds NextNumIn kN and next outbound kOut before open(). The acceptor hydrates
// from it at its first Logon, the initiator at open().
//
// CONDITIONS these cells keep, checked by reading:
//   - every frame the session writes reaches Rig::out: the config-time sink appends there, and so
//     does the CaptureTransport an mTLS acceptor cell attaches (live writes then go to it);
//   - a cell reads the numbers, the store's numbers, the store's reset calls, the writes and the
//     Disconnected entries before its frame and compares after, so a cell does not depend on where
//     the role hydrates;
//   - an Admitted cell's expected answer is spelled out below, captured from the base's run
//     (tasks.md T042) with SOH rendered as '|' (no frame here holds '|'); it is never built with a
//     production builder or read from the session;
//   - a peer identity is a subject DN whose CN is, or is not, the principal 099's rig binds
//     ("ON-LIST"), so the policy's CN extraction is the one that decides.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/logon_credentials.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "session/test_099_fixt_support.hpp"
#include "session/test_104_support.hpp"
#include "session/test_106_support.hpp"
#include "support/hooked_store.hpp"
#include "support/memory_store_test_access.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace t99 = fixpp::session::test099;
namespace t104 = fixpp::test_support::t104;
namespace t106 = fixpp::test_support::t106;
using fixpp::session::fsm_state;
using fixpp::session::reset_seqnum_policy;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::test_support::HookedStoreFactory;
using Kind = fixpp::session::SecurityProfile::kind;

// The store's NextNumIn and next outbound number before open(): distinct from each other and from
// the numbers a reset writes (1 and 2), so a number that moved cannot pass.
constexpr seqnum_t kN = 5;
constexpr seqnum_t kOut = 8;
static_assert(kN != kOut && kN > 2 && kOut > 2);

// The peer certificates' subjects: the CN of the first is the principal 099's rig binds to the
// session's target CompID, the CN of the second is bound to nothing.
constexpr std::string_view kOnListDn = "CN=ON-LIST,O=Peer,C=US";
constexpr std::string_view kOffListDn = "CN=OFF-LIST-106,O=Peer,C=US";

enum class Peer : std::uint8_t { none, on_list, off_list };
enum class Validator : std::uint8_t { none, refuses, admits };

// One cell's session and frame.
struct Spec {
    session_role role = session_role::acceptor;
    Kind profile = Kind::one_way_ca;
    Peer peer = Peer::none;  // the acceptor's attached identity
    Validator validator = Validator::none;
    bool posture = false;
    bool strict = false;
    bool reset_on_logon = false;
    std::string input;    // research R-18's input id (test_099_fixt_support.hpp's make_inputs)
    bool at_one = false;  // the frame at MsgSeqNum 1 rather than at the session's next number
};

// What the session and its store hold, read between frames.
struct Reading {
    t104::Numbers104 mgr;
    std::optional<t104::Numbers104> store;
    std::size_t store_resets = 0;  // reset() and reset_to calls issued
    std::size_t writes = 0;        // frames in Rig::out
    std::size_t disconnected = 0;  // Disconnected entries in fsm_visit_history()
    fsm_state state = fsm_state::NotConnected;
};

[[nodiscard]] std::string rendered(std::string f) {
    std::ranges::replace(f, '\x01', '|');
    return f;
}

class Cell {
public:
    explicit Cell(Spec s) : spec{std::move(s)} {
        auto st = std::make_shared<fixpp::session::MemoryStore>(fixpp::session::MemoryStore::Config{
            .policy = fixpp::session::capacity_policy::unbounded});
        fixpp::session::memory_store_test_access::set_counter(
            *st, fixpp::session::direction_t::inbound, kN);
        fixpp::session::memory_store_test_access::set_counter(
            *st, fixpp::session::direction_t::outbound, kOut);
        store->log->inner = std::move(st);
        t99::RigOptions opts;
        opts.validate = true;
        opts.role = spec.role;
        opts.mtls = spec.profile == Kind::mtls_ca;
        opts.posture = spec.posture;
        opts.tweak = [this](fixpp::session::SessionConfig& c) {
            c.store_factory = store;
            c.security_profile = fixpp::session::SecurityProfile{spec.profile};
            c.reset_on_logon = spec.reset_on_logon;
            c.reset_on_disconnect = false;
            if (spec.strict) c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_strict;
            if (spec.validator != Validator::none) {
                c.compid_authorization_policy.set_logon_validator(
                    [calls = validator_calls, admit = spec.validator == Validator::admits](
                        std::string_view, fixpp::session::logon_credentials const&) {
                        ++*calls;
                        return admit;
                    });
            }
        };
        rig = std::make_unique<t99::Rig>(t99::shape_fixture(t99::Shape::T), std::move(opts));
    }
    Cell(Cell const&) = delete;
    Cell& operator=(Cell const&) = delete;

    // open(), then the acceptor's identity attached as the Engine's accept loop attaches it.
    [[nodiscard]] ::testing::AssertionResult open() {
        if (auto f = rig->open()) return ::testing::AssertionFailure() << "open(): " << *f;
        if (spec.peer != Peer::none) {
            fixpp::transport::handshake_result hr{};
            hr.peer_id.subject_dn = spec.peer == Peer::on_list ? kOnListDn : kOffListDn;
            rig->sess->attach_accepted_transport(std::make_unique<t99::CaptureTransport>(&rig->out),
                                                 std::move(hr));
        }
        auto const want =
            spec.role == session_role::acceptor ? fsm_state::NotConnected : fsm_state::LogonSent;
        if (rig->sess->state() != want) {
            return ::testing::AssertionFailure()
                   << "after open(): " << t99::state_name(rig->sess->state());
        }
        return ::testing::AssertionSuccess();
    }

    [[nodiscard]] Reading read() {
        Reading r;
        r.mgr = t104::manager_numbers(*rig->sess);
        r.store = t104::read_store_numbers(store->log->inner.get(), rig->ioc, [this](auto pred) {
            return fixpp::test_support::pump_until(rig->ioc, std::move(pred));
        });
        r.store_resets = store->log->calls.size();
        r.writes = rig->out.size();
        r.disconnected = t106::count_disconnected(*rig->sess);
        r.state = rig->sess->state();
        return r;
    }

    // The frame's MsgSeqNum: 1, or the next number the session expects once hydrated (an
    // acceptor hydrates at the frame, so its next number is the store's).
    [[nodiscard]] seqnum_t frame_seq() const {
        if (spec.at_one) return 1;
        return spec.role == session_role::acceptor ? kN : t104::manager_numbers(*rig->sess).in;
    }

    void feed(std::string const& input_id) {
        rig->feed(t99::input(input_id).make(rig->peer_ctx(frame_seq())), &err);
    }
    void feed() { feed(spec.input); }

    [[nodiscard]] std::vector<std::string> written_since(std::size_t mark) const {
        std::vector<std::string> out;
        for (std::size_t i = mark; i < rig->out.size(); ++i) out.push_back(rendered(rig->out[i]));
        return out;
    }

    Spec spec;
    std::shared_ptr<HookedStoreFactory> store = std::make_shared<HookedStoreFactory>();
    std::shared_ptr<int> validator_calls = std::make_shared<int>(0);
    std::unique_ptr<t99::Rig> rig;
    std::string err;  // a pump miss or the arm's error, as 099's rig reports them
};

// The refusal cells' assertions (tasks.md T038 to T041; contract C-1.3): nothing written, no
// number moved in memory or in the store, no reset, Disconnected.
void expect_refused_silently(Cell& c, Reading const& before) {
    auto const after = c.read();
    EXPECT_EQ(c.written_since(before.writes), std::vector<std::string>{})
        << "frames written after the handshake";
    EXPECT_EQ(after.mgr.in, before.mgr.in) << "NextNumIn in memory";
    EXPECT_EQ(after.mgr.out, before.mgr.out) << "NextNumOut in memory";
    ASSERT_TRUE(before.store.has_value() && after.store.has_value()) << "the store was not read";
    EXPECT_EQ(after.store->in, before.store->in) << "NextNumIn in the store";
    EXPECT_EQ(after.store->out, before.store->out) << "NextNumOut in the store";
    EXPECT_EQ(after.store_resets, before.store_resets) << "store reset calls";
    EXPECT_EQ(after.state, fsm_state::Disconnected) << t99::state_name(after.state);
    EXPECT_EQ(after.disconnected, before.disconnected + 1) << "new Disconnected entries";
}

void run_refusal(Spec spec) {
    Cell c{std::move(spec)};
    ASSERT_TRUE(c.open()) << "setup";
    auto const before = c.read();
    c.feed();
    expect_refused_silently(c, before);
}

// What an Admitted cell expects after its frame: the base's answer, numbers and ending.
struct Answer {
    std::vector<std::string> frames;
    t104::Numbers104 mgr;
    t104::Numbers104 store;
    fsm_state state = fsm_state::Disconnected;
};

void expect_answer(Cell& c, Reading const& before, Answer const& want) {
    auto const after = c.read();
    EXPECT_EQ(c.written_since(before.writes), want.frames) << "the answer";
    EXPECT_EQ(after.mgr.in, want.mgr.in) << "NextNumIn in memory";
    EXPECT_EQ(after.mgr.out, want.mgr.out) << "NextNumOut in memory";
    ASSERT_TRUE(after.store.has_value()) << "the store was not read";
    EXPECT_EQ(after.store->in, want.store.in) << "NextNumIn in the store";
    EXPECT_EQ(after.store->out, want.store.out) << "NextNumOut in the store";
    EXPECT_EQ(after.state, want.state) << t99::state_name(after.state);
}

void run_admitted(Spec spec, Answer const& want) {
    Cell c{std::move(spec)};
    ASSERT_TRUE(c.open()) << "setup";
    auto const before = c.read();
    c.feed();
    expect_answer(c, before, want);
}

// The three acceptor gates of T038.
[[nodiscard]] Spec off_list_mtls(Spec s) {
    s.profile = Kind::mtls_ca;
    s.peer = Peer::off_list;
    return s;
}
[[nodiscard]] Spec refused_credentials_non_mtls(Spec s) {
    s.validator = Validator::refuses;
    return s;
}
[[nodiscard]] Spec refused_credentials_admitted_mtls(Spec s) {
    s.profile = Kind::mtls_ca;
    s.peer = Peer::on_list;
    s.validator = Validator::refuses;
    return s;
}
// T040's gate: a direct mTLS initiator started by open(), which holds no peer identity (F-3).
[[nodiscard]] Spec direct_mtls_open(Spec s) {
    s.role = session_role::initiator;
    s.profile = Kind::mtls_ca;
    return s;
}
// T042's admitted identities: an mTLS acceptor whose identity is on the list and whose validator
// admits (both halves apply and admit), and a non-mTLS initiator (no gate applies).
[[nodiscard]] Spec admitted_acceptor(Spec s) {
    s.profile = Kind::mtls_ca;
    s.peer = Peer::on_list;
    s.validator = Validator::admits;
    return s;
}
[[nodiscard]] Spec admitted_initiator(Spec s) {
    s.role = session_role::initiator;
    return s;
}

// The refusals of T038 (acceptor) and T040 (initiator), each a frame that draws it on the base.
[[nodiscard]] Spec validate_first() { return Spec{.input = "Logon 1137 absent"}; }
[[nodiscard]] Spec posture() {
    return Spec{.posture = true, .input = "posture-mismatch Logon 1137=9"};
}
[[nodiscard]] Spec sending_time() { return Spec{.input = "stale Logon 1137=9"}; }
[[nodiscard]] Spec strict() { return Spec{.strict = true, .input = "Logon 1137=9"}; }
[[nodiscard]] Spec reset_on_logon() {
    return Spec{.reset_on_logon = true, .input = "Logon 1137=9", .at_one = true};
}
[[nodiscard]] Spec s3_1137() { return Spec{.input = "Logon 1137=99"}; }
[[nodiscard]] Spec s3_initiator() { return Spec{.input = "Logon 1137=4"}; }
[[nodiscard]] Spec asked_141() {
    return Spec{.reset_on_logon = true, .input = "Logon 141=Y 1137=9"};
}
[[nodiscard]] Spec plain() { return Spec{.input = "Logon 1137=9"}; }
[[nodiscard]] Spec unreadable() { return Spec{.input = "wrongBS Logon 1137 absent"}; }

}  // namespace

// ── T038: the acceptor matrix, IdentityFirst106.<Refusal>_<Gate> (research D9 row FR-003/004) ──

TEST(IdentityFirst106, ValidateFirst_OffListMtls) { run_refusal(off_list_mtls(validate_first())); }
TEST(IdentityFirst106, ValidateFirst_RefusedCredentials_NonMtls) {
    run_refusal(refused_credentials_non_mtls(validate_first()));
}
TEST(IdentityFirst106, ValidateFirst_RefusedCredentials_AdmittedMtls) {
    run_refusal(refused_credentials_admitted_mtls(validate_first()));
}
TEST(IdentityFirst106, Posture_OffListMtls) { run_refusal(off_list_mtls(posture())); }
TEST(IdentityFirst106, Posture_RefusedCredentials_NonMtls) {
    run_refusal(refused_credentials_non_mtls(posture()));
}
TEST(IdentityFirst106, Posture_RefusedCredentials_AdmittedMtls) {
    run_refusal(refused_credentials_admitted_mtls(posture()));
}
TEST(IdentityFirst106, SendingTime_OffListMtls) { run_refusal(off_list_mtls(sending_time())); }
TEST(IdentityFirst106, SendingTime_RefusedCredentials_NonMtls) {
    run_refusal(refused_credentials_non_mtls(sending_time()));
}
TEST(IdentityFirst106, SendingTime_RefusedCredentials_AdmittedMtls) {
    run_refusal(refused_credentials_admitted_mtls(sending_time()));
}
TEST(IdentityFirst106, Strict_OffListMtls) { run_refusal(off_list_mtls(strict())); }
TEST(IdentityFirst106, Strict_RefusedCredentials_NonMtls) {
    run_refusal(refused_credentials_non_mtls(strict()));
}
TEST(IdentityFirst106, Strict_RefusedCredentials_AdmittedMtls) {
    run_refusal(refused_credentials_admitted_mtls(strict()));
}
TEST(IdentityFirst106, ResetOnLogon_OffListMtls) { run_refusal(off_list_mtls(reset_on_logon())); }
TEST(IdentityFirst106, ResetOnLogon_RefusedCredentials_NonMtls) {
    run_refusal(refused_credentials_non_mtls(reset_on_logon()));
}
TEST(IdentityFirst106, ResetOnLogon_RefusedCredentials_AdmittedMtls) {
    run_refusal(refused_credentials_admitted_mtls(reset_on_logon()));
}
TEST(IdentityFirst106, S3_1137_OffListMtls) { run_refusal(off_list_mtls(s3_1137())); }
TEST(IdentityFirst106, S3_1137_RefusedCredentials_NonMtls) {
    run_refusal(refused_credentials_non_mtls(s3_1137()));
}
TEST(IdentityFirst106, S3_1137_RefusedCredentials_AdmittedMtls) {
    run_refusal(refused_credentials_admitted_mtls(s3_1137()));
}

// ── T039: unreadable credentials with a validator installed (contract C-1.4) ──────────────────
//
// The frame's BeginString is not the session's, so interpret_logon refuses it, and its absent
// 1137 draws the validate-first Reject. The installed validator admits: the refusal is the
// fail-closed rule for an unreadable input, and the validator is never called with such a frame.

namespace {
void unreadable_with_validator(Spec s) {
    s.validator = Validator::admits;
    Cell c{std::move(s)};
    ASSERT_TRUE(c.open()) << "setup";
    auto const before = c.read();
    c.feed();
    expect_refused_silently(c, before);
    EXPECT_EQ(*c.validator_calls, 0) << "validator calls";
}
}  // namespace

TEST(IdentityFirst106, ValidateFirst_UnreadableCredentials_NonMtls) {
    unreadable_with_validator(unreadable());
}
TEST(IdentityFirst106, ValidateFirst_UnreadableCredentials_AdmittedMtls) {
    auto s = unreadable();
    s.profile = Kind::mtls_ca;
    s.peer = Peer::on_list;
    unreadable_with_validator(std::move(s));
}

// ── T040: a direct mTLS initiator started by open() (contract C-1.7) ──────────────────────────

TEST(IdentityFirst106, ValidateFirst_DirectMtlsOpen) {
    run_refusal(direct_mtls_open(validate_first()));
}
TEST(IdentityFirst106, Posture_DirectMtlsOpen) { run_refusal(direct_mtls_open(posture())); }
TEST(IdentityFirst106, SendingTime_DirectMtlsOpen) {
    run_refusal(direct_mtls_open(sending_time()));
}
TEST(IdentityFirst106, S3_DirectMtlsOpen) { run_refusal(direct_mtls_open(s3_initiator())); }
TEST(IdentityFirst106, Asked141_DirectMtlsOpen) { run_refusal(direct_mtls_open(asked_141())); }

// ── T041: the plain and strict refusals (research D9 row "FR-003 plain", N-5) ─────────────────
//
// A well-formed, in-sequence, unauthorised Logon or acknowledgement with no other fault.

TEST(IdentityFirst106, Plain_Acceptor_OffListMtls) { run_refusal(off_list_mtls(plain())); }
TEST(IdentityFirst106, Plain_Acceptor_RefusedCredentials) {
    run_refusal(refused_credentials_non_mtls(plain()));
}
TEST(IdentityFirst106, Plain_Initiator_DirectMtlsOpen) { run_refusal(direct_mtls_open(plain())); }
TEST(IdentityFirst106, Strict_Initiator_DirectMtlsOpen) { run_refusal(direct_mtls_open(strict())); }

// ── T042: admitted identities (research D9 row FR-005; contract C-1.5) ────────────────────────
//
// [guard: M1b] for the Admitted_<Refusal>_<Role> cells: the answer is the base's, captured on the
// base and spelled out here.

namespace {
// PIN kAdmittedValidateFirstAcceptor
const Answer kAdmittedValidateFirstAcceptor{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedPostureAcceptor
const Answer kAdmittedPostureAcceptor{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedSendingTimeAcceptor
const Answer kAdmittedSendingTimeAcceptor{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedStrictAcceptor
const Answer kAdmittedStrictAcceptor{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedResetOnLogonAcceptor
const Answer kAdmittedResetOnLogonAcceptor{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedS31137Acceptor
const Answer kAdmittedS31137Acceptor{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedValidateFirstInitiator
const Answer kAdmittedValidateFirstInitiator{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedPostureInitiator
const Answer kAdmittedPostureInitiator{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedSendingTimeInitiator
const Answer kAdmittedSendingTimeInitiator{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedS3Initiator
const Answer kAdmittedS3Initiator{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedAsked141Initiator
const Answer kAdmittedAsked141Initiator{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
// PIN kAdmittedUnreadableNoValidator
const Answer kAdmittedUnreadableNoValidator{
    .frames = {}, .mgr = {}, .store = {}, .state = fsm_state::NotConnected};
}  // namespace

TEST(IdentityFirst106, Admitted_ValidateFirst_Acceptor) {
    run_admitted(admitted_acceptor(validate_first()), kAdmittedValidateFirstAcceptor);
}
TEST(IdentityFirst106, Admitted_Posture_Acceptor) {
    run_admitted(admitted_acceptor(posture()), kAdmittedPostureAcceptor);
}
TEST(IdentityFirst106, Admitted_SendingTime_Acceptor) {
    run_admitted(admitted_acceptor(sending_time()), kAdmittedSendingTimeAcceptor);
}
TEST(IdentityFirst106, Admitted_Strict_Acceptor) {
    run_admitted(admitted_acceptor(strict()), kAdmittedStrictAcceptor);
}
TEST(IdentityFirst106, Admitted_ResetOnLogon_Acceptor) {
    run_admitted(admitted_acceptor(reset_on_logon()), kAdmittedResetOnLogonAcceptor);
}
TEST(IdentityFirst106, Admitted_S3_1137_Acceptor) {
    run_admitted(admitted_acceptor(s3_1137()), kAdmittedS31137Acceptor);
}
TEST(IdentityFirst106, Admitted_ValidateFirst_Initiator) {
    run_admitted(admitted_initiator(validate_first()), kAdmittedValidateFirstInitiator);
}
TEST(IdentityFirst106, Admitted_Posture_Initiator) {
    run_admitted(admitted_initiator(posture()), kAdmittedPostureInitiator);
}
TEST(IdentityFirst106, Admitted_SendingTime_Initiator) {
    run_admitted(admitted_initiator(sending_time()), kAdmittedSendingTimeInitiator);
}
TEST(IdentityFirst106, Admitted_S3_Initiator) {
    run_admitted(admitted_initiator(s3_initiator()), kAdmittedS3Initiator);
}
TEST(IdentityFirst106, Admitted_Asked141_Initiator) {
    run_admitted(admitted_initiator(asked_141()), kAdmittedAsked141Initiator);
}

// [guard: M1] A validate-first Reject that keeps the session in its arm (RefTagID 5999, not 1137,
// 1130 or 372) leaves the identity unconsumed, so a corrected Logon on the same connection is
// judged afresh and reaches Active (F-2).
TEST(IdentityFirst106, Admitted_StayInArmReject_NextLogonActive) {
    Cell c{admitted_acceptor(Spec{})};
    ASSERT_TRUE(c.open()) << "setup";
    auto const before = c.read();
    c.feed("Logon undeclared 5999");
    auto const rejects = c.written_since(before.writes);
    ASSERT_EQ(rejects.size(), 1U) << "frames written for the first Logon";
    EXPECT_EQ(t99::tag_value(c.rig->out[before.writes], 35), "3") << rejects.front();
    EXPECT_EQ(t99::tag_value(c.rig->out[before.writes], 371), "5999") << rejects.front();
    ASSERT_EQ(c.rig->sess->state(), fsm_state::NotConnected)
        << "after the first Logon: " << t99::state_name(c.rig->sess->state());
    c.feed("Logon 1137=9");
    EXPECT_EQ(c.rig->sess->state(), fsm_state::Active)
        << "after the corrected Logon: " << t99::state_name(c.rig->sess->state());
}

// [guard: M1c] A non-mTLS acceptor with the validate gate on and no logon validator installed:
// a Logon interpret_logon refuses (its BeginString) draws the validate-first Reject, as on the
// base (the credentials half admits an unreadable input when no validator is installed).
TEST(IdentityFirst106, Admitted_UnreadableNoValidator_ValidateFirstReject) {
    run_admitted(unreadable(), kAdmittedUnreadableNoValidator);
}
