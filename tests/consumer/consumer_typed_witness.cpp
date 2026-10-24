// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/consumer_typed_witness.cpp — #613 C-1 (`.specify/613-builders-export.md` §4.1).
//
// An INSTALLED consumer of the typed builder/validator tier, linked by name
// (fixpp::builders::<ver> / fixpp::validators::<ver>, never fixpp::fixpp), one message
// per version: v42/v44 NewOrderSingle, v50sp2/vlatest ListStatus. For each version it
// checks three things:
//   1. build_<Msg> returns a body BYTE-FOR-BYTE equal to a checked-in literal;
//   2. validate_<Msg> accepts the same inputs, every required field set;
//   3. validate_<Msg> refuses them with one named required field cleared.
// 2 and 3 together prove a real validate_ body ran rather than a stub.
//
// ⚠️ THE LITERALS ARE WRITTEN FROM SOURCES, NEVER CAPTURED FROM A RUN. A literal pasted
// from this program's output would read its expectation from the thing it checks. The
// sources, read as text:
//   * tag ORDER — the emission order of that version's generated <Msg>.builder.inl
//     (the emitter's order, which need not be the dictionary's member order);
//   * FRAMING — include/fixpp/wire/body_builder.hpp's design block: body only,
//     "35=<msg_type>\x01" first (emitted at commit()), "<tag>=<value>\x01" per field,
//     the group count before its instances; as pinned by tests/wire/test_body_builder.cpp;
//   * DECIMAL text — .specify/2a-decimal.md §6.2: a zero mantissa writes "0", and an
//     exponent-0 value writes its mantissa's digits. Only exponent-0 decimals are used.
//   * the REQUIRED sets — the dictionary (FIX42.xml / FIX44.xml / FIX50SP2.xml
//     required="Y"; the Orchestra presence="required" for vlatest).
// Re-derive a version's tag order with
//   grep -oE 'bb\.(field|group_begin)\([0-9]+|set_[a-z]+\([0-9]+' <ver>/messages/<Msg>.builder.inl
// in a configured tree's _codegen/include/fixpp/, keeping the tags this file sets.
// If a literal and the program disagree, re-read those sources; do not copy the bytes.
//
// Header-only inline mode would satisfy the link WITHOUT the archive (a spurious HIT),
// so this TU refuses every macro that switches the four messages it uses inline.

#if defined(FIXPP_BUILDERS_HEADER_ONLY) || defined(FIXPP_VALIDATORS_HEADER_ONLY) ||       \
    defined(FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle) ||                                  \
    defined(FIXPP_VALIDATORS_HEADER_ONLY_NewOrderSingle) ||                                \
    defined(FIXPP_BUILDERS_HEADER_ONLY_ListStatus) || defined(FIXPP_VALIDATORS_HEADER_ONLY_ListStatus)
#error "FIXPP_613_C1_HEADER_ONLY_MACRO: C-1 must link the typed archives, not inline them"
#endif

#ifndef FIXPP_613_HAS_VLATEST
#error "FIXPP_613_HAS_VLATEST must be defined (0 or 1) by tests/consumer/CMakeLists.txt"
#endif

#include <fixpp/v42/messages/NewOrderSingle.hpp>
#include <fixpp/v44/messages/NewOrderSingle.hpp>
#include <fixpp/v50sp2/messages/ListStatus.hpp>
#if FIXPP_613_HAS_VLATEST
#include <fixpp/vlatest/messages/ListStatus.hpp>
#endif

#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;

void fail(char const* ver, char const* what) {
    std::printf("FAIL: %s: %s\n", ver, what);
    ++g_failures;
}

std::string_view as_text(std::span<std::byte> s) {
    return {reinterpret_cast<char const*>(s.data()), s.size()};
}

// One version's three checks. `build` and `validate` are the version's functions;
// `cleared` is `args` with one required field removed.
template <class Args, class Build, class Validate>
void check(char const* ver, Args const& args, Args const& cleared, std::string_view expected,
           Build build, Validate validate) {
    std::array<std::byte, 4096> buf{};
    auto built = build(std::span<std::byte>{buf}, args);
    if (!built) {
        fail(ver, "build_ returned an error");
    } else if (as_text(*built) != expected) {
        std::printf("FAIL: %s: build_ body differs from the literal\n  got:      ", ver);
        for (char c : as_text(*built)) {
            std::printf("%c", c == '\x01' ? '|' : c);
        }
        std::printf("\n  expected: ");
        for (char c : expected) {
            std::printf("%c", c == '\x01' ? '|' : c);
        }
        std::printf("\n");
        ++g_failures;
    }
    if (!validate(args)) {
        fail(ver, "validate_ refused inputs with every required field set");
    }
    if (validate(cleared)) {
        fail(ver, "validate_ accepted inputs with a required field cleared");
    }
}

fixpp::decimal_t dec(std::int64_t mantissa) {
    return fixpp::decimal_t{fixpp::core::pod_decimal{.mantissa = mantissa, .exponent = 0}};
}

}  // namespace

int main() {
    // ── v42 NewOrderSingle (35=D): every field FIX42.xml marks required="Y" is set;
    // the cleared one is ClOrdID(11). Tag order: v42 NewOrderSingle.builder.inl.
    {
        fixpp::v42::NewOrderSingleArgs a{};
        a.cl_ord_id = "CL42";
        a.handl_inst = '1';
        a.symbol = "IBM";
        a.side = '1';
        a.transact_time = "20261024-12:00:00";
        a.ord_type = '1';
        auto cleared = a;
        cleared.cl_ord_id.reset();
        check("v42", a, cleared,
              "35=D\x01"
              "11=CL42\x01"
              "21=1\x01"
              "40=1\x01"
              "54=1\x01"
              "55=IBM\x01"
              "60=20261024-12:00:00\x01",
              [](std::span<std::byte> o, auto const& x) { return fixpp::v42::build_NewOrderSingle(o, x); },
              [](auto const& x) { return fixpp::v42::validate_NewOrderSingle(x).has_value(); });
    }

    // ── v44 NewOrderSingle (35=D): every top-level field FIX44.xml marks required="Y"
    // is set; the cleared one is ClOrdID(11). Tag order: v44 NewOrderSingle.builder.inl.
    {
        fixpp::v44::NewOrderSingleArgs a{};
        a.cl_ord_id = "CL44";
        a.handl_inst = '1';
        a.symbol = "IBM";
        a.side = '2';
        a.transact_time = "20261024-12:00:01";
        a.ord_type = '2';
        auto cleared = a;
        cleared.cl_ord_id.reset();
        check("v44", a, cleared,
              "35=D\x01"
              "11=CL44\x01"
              "21=1\x01"
              "40=2\x01"
              "54=2\x01"
              "55=IBM\x01"
              "60=20261024-12:00:01\x01",
              [](std::span<std::byte> o, auto const& x) { return fixpp::v44::build_NewOrderSingle(o, x); },
              [](auto const& x) { return fixpp::v44::validate_NewOrderSingle(x).has_value(); });
    }

    // ── v50sp2 ListStatus (35=N): every top-level field FIX50SP2.xml marks
    // required="Y" is set, and one NoOrders(73) entry (the required OrdListStatGrp)
    // with the entry fields vlatest requires; the cleared one is ListID(66). Tag order,
    // group entry included: v50sp2 ListStatus.builder.inl.
    {
        fixpp::v50sp2::groups::G_73_5Args order{};
        order.cl_ord_id = "C50";
        order.cum_qty = dec(0);
        order.ord_status = '0';
        order.leaves_qty = dec(5);
        order.cxl_qty = dec(0);
        order.avg_px = dec(0);
        std::array<fixpp::v50sp2::groups::G_73_5Args, 1> orders{order};
        fixpp::v50sp2::ListStatusArgs a{};
        a.list_id = "L50";
        a.tot_no_orders = 1;
        a.orders = std::span<const fixpp::v50sp2::groups::G_73_5Args>{orders};
        a.no_rpts = 1;
        a.rpt_seq = 1;
        a.list_status_type = 2;
        a.list_order_status = 3;
        auto cleared = a;
        cleared.list_id.reset();
        check("v50sp2", a, cleared,
              "35=N\x01"
              "66=L50\x01"
              "68=1\x01"
              "73=1\x01"
              "11=C50\x01"
              "14=0\x01"
              "39=0\x01"
              "151=5\x01"
              "84=0\x01"
              "6=0\x01"
              "82=1\x01"
              "83=1\x01"
              "429=2\x01"
              "431=3\x01",
              [](std::span<std::byte> o, auto const& x) { return fixpp::v50sp2::build_ListStatus(o, x); },
              [](auto const& x) { return fixpp::v50sp2::validate_ListStatus(x).has_value(); });
    }

#if FIXPP_613_HAS_VLATEST
    // ── vlatest ListStatus (35=N): every field and the NoOrders(73) group the
    // Orchestra marks presence="required" are set, with the entry fields the generated
    // traits require; the cleared one is ListID(66). Tag order: vlatest
    // ListStatus.builder.inl.
    {
        fixpp::vlatest::groups::G_73_5Args order{};
        order.cl_ord_id = "CLX";
        order.cum_qty = dec(0);
        order.ord_status = '0';
        order.leaves_qty = dec(7);
        order.cxl_qty = dec(0);
        order.avg_px = dec(0);
        std::array<fixpp::vlatest::groups::G_73_5Args, 1> orders{order};
        fixpp::vlatest::ListStatusArgs a{};
        a.list_id = "LX";
        a.tot_no_orders = 1;
        a.orders = std::span<const fixpp::vlatest::groups::G_73_5Args>{orders};
        a.no_rpts = 1;
        a.rpt_seq = 2;
        a.list_status_type = 2;
        a.list_order_status = 3;
        auto cleared = a;
        cleared.list_id.reset();
        check("vlatest", a, cleared,
              "35=N\x01"
              "66=LX\x01"
              "68=1\x01"
              "73=1\x01"
              "11=CLX\x01"
              "14=0\x01"
              "39=0\x01"
              "151=7\x01"
              "84=0\x01"
              "6=0\x01"
              "82=1\x01"
              "83=2\x01"
              "429=2\x01"
              "431=3\x01",
              [](std::span<std::byte> o, auto const& x) { return fixpp::vlatest::build_ListStatus(o, x); },
              [](auto const& x) { return fixpp::vlatest::validate_ListStatus(x).has_value(); });
    }
#endif

    if (g_failures != 0) {
        std::printf("FAIL: consumer_typed_witness: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("PASS: consumer_typed_witness: build_/validate_ per version (%s)\n",
                FIXPP_613_HAS_VLATEST ? "v42 v44 v50sp2 vlatest" : "v42 v44 v50sp2");
    return 0;
}
