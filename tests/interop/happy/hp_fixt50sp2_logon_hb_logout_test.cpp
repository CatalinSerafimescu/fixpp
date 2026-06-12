// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fixt50sp2_logon_hb_logout_test.cpp — 033 US3 (live).
//
// The FIXT.1.1 live establishment cell family: Logon → idle Heartbeat → Logout,
// over TLS, with BeginString(8)=FIXT.1.1 and DefaultApplVerID(1137) advertising
// the application version. The headline SC-004 conformance proof for 033: fixpp
// establishes a FIXT session against a real QuickFIX peer that validates the
// outbound Logon against FIXT11.xml (session) + the app dictionary.
//
// Parametrized over (counterparty × role × application_version):
//   - application_version ∈ {v50sp2 (1137=9), v44 (1137=6)} — the SC-006
//     version-generality proof: two distinct app versions establish through the
//     SAME code path (no version-specific establishment branch). 4.4-over-FIXT is
//     the FIXT-carrying-FIX.4.4 axis SC-004 names alongside FIX.5.0SP2.
// Structurally identical to hp_fix44_logon_hb_logout_test.cpp (the canonical US1
// driver) — the ONLY delta is the SessionConfig version axis: begin_string set to
// "FIXT.1.1" and default_appl_ver_id set per the version param (the is_fixt()
// predicate), so the engine emits the FIXT Logon split (derive_logon_fixt_fields).
//
// Counterparty-required (FR-023): absent ⇒ GTEST_SKIP, never a silent pass. The
// parent harness (run_interop_cell.py, HP-*-fixt11-{fix50sp2,fix44} cells) brings
// up a QuickFIX SSL peer configured for FIXT.1.1 + the matching app dictionary.
//
// spec_ref [033 US3 SC-004/SC-006 / FR-001 / FR-012; FIXT.1.1 §Logon].
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <string>
#include <tuple>
#include <vector>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::dict::application_version;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

using FixtParam = std::tuple<Counterparty, Role, application_version>;

// ── Engine app-version registry (acceptor serviceability, FR-004a/C5) ─────────
//
// fixpp's acceptor refuses an inbound FIXT Logon whose DefaultApplVerID(1137)
// resolves to a version with NO application dictionary in the engine's
// version_registry (Reject 373=5; session.cpp:2004). The registry is built from
// EngineConfig::dictionaries at Engine construction. A real FIXT-acceptor
// deployment registers the app dictionaries it services; the interop fixture must
// do the same or the acceptor cells reject a version they are configured to speak.
// (The initiator path performs NO serviceability gate — FR-004a is acceptor-only —
// so the registry is harmless for init cells, but the engine is shared, so we set
// it unconditionally.) Two version-tagged dicts (the XmlLoader maps
// <fix major="5" minor="0" servicepack="2"> → v50sp2, <… 4 4> → v44) so both the
// fix50sp2 and fix44-over-FIXT acceptor cells resolve serviceable.
// [mirrors tests/session/test_fixt_logon_establishment.cpp make_dict + FixtSetup]
constexpr std::string_view kFixt44Xml = R"xml(
<fix major="4" minor="4">
  <header><field number="8" name="BeginString" required="Y"/></header>
  <trailer><field number="10" name="CheckSum" required="Y"/></trailer>
  <messages>
    <message name="Heartbeat" msgtype="0" msgcat="admin">
      <field number="112" name="TestReqID" required="N"/>
    </message>
  </messages>
  <fields>
    <field number="8" name="BeginString" type="STRING"/>
    <field number="10" name="CheckSum" type="STRING"/>
    <field number="112" name="TestReqID" type="STRING"/>
  </fields>
</fix>
)xml";

constexpr std::string_view kFixt50sp2Xml = R"xml(
<fix major="5" minor="0" servicepack="2">
  <header><field number="8" name="BeginString" required="Y"/></header>
  <trailer><field number="10" name="CheckSum" required="Y"/></trailer>
  <messages>
    <message name="Heartbeat" msgtype="0" msgcat="admin">
      <field number="112" name="TestReqID" required="N"/>
    </message>
  </messages>
  <fields>
    <field number="8" name="BeginString" type="STRING"/>
    <field number="10" name="CheckSum" type="STRING"/>
    <field number="112" name="TestReqID" type="STRING"/>
  </fields>
</fix>
)xml";

[[nodiscard]] std::shared_ptr<const fixpp::dict::Dictionary> make_dict(std::string_view xml) {
    constexpr std::size_t kBufSize = 64u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    fixpp::dict::Dictionary d = fixpp::dict::XmlLoader{}.load_from_string(xml, mr);
    auto* raw_dict = new fixpp::dict::Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](const fixpp::dict::Dictionary* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};
}

// EngineConfig with both FIXT app dictionaries registered (clock is injected by
// the fixture when left null).
[[nodiscard]] fixpp::core::EngineConfig make_fixt_engine_config() {
    fixpp::core::EngineConfig cfg;
    cfg.dictionaries = {make_dict(kFixt44Xml), make_dict(kFixt50sp2Xml)};
    return cfg;
}

class HappyFixtLogonHbLogout : public ::testing::TestWithParam<FixtParam> {};

// Param-name formatter: "QFcpp_init_fix50sp2" / "QFj_acc_fix44". The parent cell's
// gtest_filter suffix MUST match this verbatim (a no-match filter masquerades as a
// pass — run_interop_cell.py parse_gtest_status).
std::string fixt_cell_name(const ::testing::TestParamInfo<FixtParam>& info) {
    const auto [cp, role, ver] = info.param;
    std::string n = (cp == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    n += (role == Role::fixpp_initiator) ? "_init" : "_acc";
    n += (ver == application_version::v50sp2) ? "_fix50sp2" : "_fix44";
    return n;
}

TEST_P(HappyFixtLogonHbLogout, LogonHeartbeatLogout) {
    const auto [counterparty, role, appver] = GetParam();
    namespace hp = fixpp::interop::hp;

    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    const auto endpoint = hp::cell_endpoint(counterparty, role);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    fixpp::interop::InteropEngineFixture fx{make_fixt_engine_config()};
    auto cfg = hp::make_session_config(role, "FIXT.1.1", factory, fx.ioc().get_executor(),
                                       *endpoint);
    // The FIXT axis (the only delta vs the FIX.4.4 cell): advertise the application
    // version. begin_string=="FIXT.1.1" + this field set ⇒ is_fixt() holds ⇒ the
    // engine emits Logon(8=FIXT.1.1, 1137=<wire id>). v50sp2 → 1137=9; v44 → 1137=6.
    cfg.default_appl_ver_id = appver;
    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── Logon: drive to Active over FIXT.1.1 ─────────────────────────────────
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "FIXT session did not reach Active (logon) against "
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    // ── Seqnum delta (FR-007): outbound advanced past the Logon ──────────────
    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // ── SC-006: the negotiated application version equals the configured one ──
    // Locks the 1137 negotiation in a COMMITTED witness (the wire 1137 value lives
    // only in the gitignored golden). Both roles record negotiated_appl_version_
    // (initiator from the Logon-ack 1137; acceptor from the inbound Logon 1137);
    // the peer advertises the same version, so it equals `appver`. This is the
    // version-general proof: v50sp2 and v44 both negotiate through one code path.
    EXPECT_EQ(s->negotiated_version_profile().default_appl, appver)
        << "negotiated application version must equal the configured/advertised version (SC-006)";

    // ── Logout: graceful stop emits Logout + closes; bounded (FR-004) ────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fixt, HappyFixtLogonHbLogout,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor),
                       ::testing::Values(application_version::v50sp2, application_version::v44)),
    fixt_cell_name);

}  // namespace
