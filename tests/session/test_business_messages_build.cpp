// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_business_messages_build.cpp
//
// 020-g2-business-messages T005 — builder unit tests (RED first, GREEN at T008).
//
// Named tests (data-model.md INV-2/3/4 + tasks.md T005):
//   AS1_NOS_HappyPath_ParseBack           — build NOS, parse fields back, check fidelity
//   AS2_ExecRpt_HappyPath_ParseBack        — build ExecRpt, parse fields back, check fidelity
//   Builder_Output_ContainsNoEngineTags    — INV-2: body has no 8=/9=/34=/49=/52=/56=/10=
//   Builder_NumericFidelity_DecimalValueEquality — INV-3: trailing-zero form equivalence
//   Builder_InvalidField_NoUsableOutput    — INV-4: invalid inputs return unexpected
//   Builder_FR008_TransactTime_ShapeRejection — FR-008 UTCTimestamp validation
//   Builder_NoHeap_CountingResource        — [const §VIII.5] zero global allocations
//
// Alloc witness strategy: override global operator new/new[] to count allocations,
// assert counter is UNCHANGED across both build_* calls.
// (A counting_resource PMR alone would be a false-pass: the builders take no mr param,
// so nothing routes through it. Only global new interception is honest here.
// mallocnesia LD_PRELOAD is the CI-tier cross-check per
// [feedback_tracking_pmr_resource_false_pass].)
//
// decimal_t is constructed via decimal_t::parse(bytes, mr) — no string-literal ctor.
//
// Anchors: contracts/business-messages.md; data-model.md E1/E2; INV-2/3/4; FR-008.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/business_messages.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

// ── Global operator new counter ────────────────────────────────────────────────
// Counts allocations routed through global operator new (not PMR). This is the
// honest gate for [const §VIII.5] on builders that take no memory_resource param.
// mallocnesia LD_PRELOAD is the CI-tier complement (intercepts malloc from any path).
//
// ASan and TSan ship their own strong operator new/delete: ASan trips a runtime
// alloc-dealloc-mismatch and TSan multiply-defines operator new at link if we
// replace them. Under those sanitizers we compile out the replacement and skip
// the alloc witness below (mallocnesia is the CI-tier cross-check); the builder
// correctness tests still run. UBSan is unaffected and keeps the full witness.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

#if !FIXPP_SANITIZER_REPLACES_NEW
namespace {

std::atomic<long> g_alloc_count{0};

}  // namespace

void* operator new(std::size_t size) {
    ++g_alloc_count;
    void* p = std::malloc(size);
    if (!p) {
        throw std::bad_alloc{};
    }
    return p;
}

void* operator new[](std::size_t size) {
    ++g_alloc_count;
    void* p = std::malloc(size);
    if (!p) {
        throw std::bad_alloc{};
    }
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif  // !FIXPP_SANITIZER_REPLACES_NEW

namespace {

using fixpp::decimal_t;

// Helper: parse a decimal_t from an ASCII string literal.
// Caller supplies an arena. This allocates via mr (not via global new).
decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

// Helper: search the body bytes for a tag token "TAG=" at a field boundary.
// Fields in FIX are separated by SOH (0x01). A tag token must appear either at
// the very start of the buffer OR immediately after a SOH byte.
// Returns true if found (presence check).
bool body_contains_tag(std::span<const std::byte> body, std::string_view tag_str) {
    // Build the token we're looking for as bytes: "tag="
    std::string token{tag_str};
    token += '=';
    if (body.size() < token.size()) return false;

    for (std::size_t i = 0; i + token.size() <= body.size(); ++i) {
        // Must be at start or after SOH
        bool at_boundary = (i == 0) || (static_cast<unsigned char>(body[i - 1]) == 0x01u);
        if (!at_boundary) continue;
        bool match = true;
        for (std::size_t j = 0; j < token.size(); ++j) {
            if (static_cast<char>(body[i + j]) != token[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Helper: extract the value bytes for the first occurrence of tag in the body.
// Returns empty span if not found.
std::string_view extract_field_value(std::span<const std::byte> body, std::string_view tag_str) {
    std::string token{tag_str};
    token += '=';
    for (std::size_t i = 0; i + token.size() <= body.size(); ++i) {
        bool at_boundary = (i == 0) || (static_cast<unsigned char>(body[i - 1]) == 0x01u);
        if (!at_boundary) continue;
        bool match = true;
        for (std::size_t j = 0; j < token.size(); ++j) {
            if (static_cast<char>(body[i + j]) != token[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            std::size_t val_start = i + token.size();
            std::size_t val_end = val_start;
            while (val_end < body.size() && static_cast<unsigned char>(body[val_end]) != 0x01u) {
                ++val_end;
            }
            return std::string_view{reinterpret_cast<const char*>(body.data() + val_start),
                                    val_end - val_start};
        }
    }
    return {};
}

constexpr std::size_t kBufSize = 1024;

}  // namespace

// ── AS1: NOS happy-path parse-back ─────────────────────────────────────────────
TEST(BusinessMessagesBuild, AS1_NOS_HappyPath_ParseBack) {
    std::pmr::monotonic_buffer_resource arena{4096};

    auto order_qty = make_decimal("100", &arena);
    auto price = make_decimal("190.5", &arena);

    std::array<std::byte, kBufSize> buf{};
    auto result = fixpp::session::build_new_order_single(
        std::span<std::byte>{buf}, "ORD-001", "MSFT", '1', order_qty, price, "20240101-10:00:00");

    ASSERT_TRUE(result.has_value()) << "build_new_order_single failed unexpectedly";
    std::span<std::byte> body = *result;
    ASSERT_FALSE(body.empty());

    // Check MsgType is 35=D as first field
    EXPECT_TRUE(body_contains_tag(body, "35"));
    EXPECT_EQ(extract_field_value(body, "35"), "D");

    // Check all required fields are present
    EXPECT_EQ(extract_field_value(body, "11"), "ORD-001");
    EXPECT_EQ(extract_field_value(body, "55"), "MSFT");
    EXPECT_EQ(extract_field_value(body, "54"), "1");
    EXPECT_EQ(extract_field_value(body, "40"), "2");  // OrdType fixed Limit
    EXPECT_EQ(extract_field_value(body, "60"), "20240101-10:00:00");

    // Numeric fidelity: parse back OrderQty and Price
    auto qty_sv = extract_field_value(body, "38");
    auto price_sv = extract_field_value(body, "44");
    ASSERT_FALSE(qty_sv.empty());
    ASSERT_FALSE(price_sv.empty());

    auto qty_bytes = std::span<const std::byte>{reinterpret_cast<const std::byte*>(qty_sv.data()),
                                                qty_sv.size()};
    auto price_bytes = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(price_sv.data()), price_sv.size()};

    auto parsed_qty = decimal_t::parse(qty_bytes, &arena);
    auto parsed_price = decimal_t::parse(price_bytes, &arena);
    ASSERT_TRUE(parsed_qty.has_value());
    ASSERT_TRUE(parsed_price.has_value());
    EXPECT_EQ(order_qty, *parsed_qty);
    EXPECT_EQ(price, *parsed_price);
}

// ── AS2: ExecRpt happy-path parse-back ─────────────────────────────────────────
TEST(BusinessMessagesBuild, AS2_ExecRpt_HappyPath_ParseBack) {
    std::pmr::monotonic_buffer_resource arena{4096};

    auto leaves_qty = make_decimal("0", &arena);
    auto cum_qty = make_decimal("100", &arena);
    auto avg_px = make_decimal("190.5", &arena);

    std::array<std::byte, kBufSize> buf{};
    auto result =
        fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD-XCH-001", "EXEC-001",
                                               'F', '2', "MSFT", '1', leaves_qty, cum_qty, avg_px);

    ASSERT_TRUE(result.has_value()) << "build_execution_report failed unexpectedly";
    std::span<std::byte> body = *result;
    ASSERT_FALSE(body.empty());

    // MsgType must be 35=8
    EXPECT_EQ(extract_field_value(body, "35"), "8");

    // Check all required fields
    EXPECT_EQ(extract_field_value(body, "37"), "ORD-XCH-001");
    EXPECT_EQ(extract_field_value(body, "17"), "EXEC-001");
    EXPECT_EQ(extract_field_value(body, "150"), "F");
    EXPECT_EQ(extract_field_value(body, "39"), "2");
    EXPECT_EQ(extract_field_value(body, "55"), "MSFT");
    EXPECT_EQ(extract_field_value(body, "54"), "1");

    // Numeric fidelity
    auto lqty_sv = extract_field_value(body, "151");
    auto cqty_sv = extract_field_value(body, "14");
    auto apx_sv = extract_field_value(body, "6");
    ASSERT_FALSE(lqty_sv.empty());
    ASSERT_FALSE(cqty_sv.empty());
    ASSERT_FALSE(apx_sv.empty());

    auto pl = decimal_t::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(lqty_sv.data()),
                                   lqty_sv.size()},
        &arena);
    auto pc = decimal_t::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(cqty_sv.data()),
                                   cqty_sv.size()},
        &arena);
    auto pa = decimal_t::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(apx_sv.data()),
                                   apx_sv.size()},
        &arena);
    ASSERT_TRUE(pl.has_value());
    ASSERT_TRUE(pc.has_value());
    ASSERT_TRUE(pa.has_value());
    EXPECT_EQ(leaves_qty, *pl);
    EXPECT_EQ(cum_qty, *pc);
    EXPECT_EQ(avg_px, *pa);
}

// ── INV-2: body must contain no engine-stamped tags ─────────────────────────────
// Scans the produced body and asserts no "8=", "9=", "34=", "49=", "52=", "56=",
// "10=" appear at a field-start boundary. These tags are engine-stamped; their
// presence in the body is a defect (contracts/business-messages.md INV-2).
//
// RC#1 hardening (gate-b/r1): builds with SOH-injected string values MUST be
// rejected by the builder — the injection is not embedded. We assert the builder
// returns unexpected so the injected tag never enters the body (fail-closed).
TEST(BusinessMessagesBuild, Builder_Output_ContainsNoEngineTags) {
    std::pmr::monotonic_buffer_resource arena{4096};

    auto qty = make_decimal("50", &arena);
    auto px = make_decimal("100.25", &arena);

    std::array<std::byte, kBufSize> nos_buf{};
    auto nos_r = fixpp::session::build_new_order_single(std::span<std::byte>{nos_buf}, "CLORD1",
                                                        "AAPL", '2', qty, px, "20240601-09:30:00");
    ASSERT_TRUE(nos_r.has_value());
    std::span<std::byte> nos_body = *nos_r;

    // Engine-stamped tags must NOT appear at any field-start boundary
    EXPECT_FALSE(body_contains_tag(nos_body, "8")) << "NOS body contains 8= (BeginString)";
    EXPECT_FALSE(body_contains_tag(nos_body, "9")) << "NOS body contains 9= (BodyLength)";
    EXPECT_FALSE(body_contains_tag(nos_body, "34")) << "NOS body contains 34= (MsgSeqNum)";
    EXPECT_FALSE(body_contains_tag(nos_body, "49")) << "NOS body contains 49= (SenderCompID)";
    EXPECT_FALSE(body_contains_tag(nos_body, "52")) << "NOS body contains 52= (SendingTime)";
    EXPECT_FALSE(body_contains_tag(nos_body, "56")) << "NOS body contains 56= (TargetCompID)";
    EXPECT_FALSE(body_contains_tag(nos_body, "10")) << "NOS body contains 10= (CheckSum)";

    // Same for ExecutionReport
    auto lq = make_decimal("0", &arena);
    auto cq = make_decimal("50", &arena);
    auto ap = make_decimal("100.25", &arena);

    std::array<std::byte, kBufSize> er_buf{};
    auto er_r = fixpp::session::build_execution_report(
        std::span<std::byte>{er_buf}, "ORD-001", "EXEC-001", 'F', '2', "AAPL", '2', lq, cq, ap);
    ASSERT_TRUE(er_r.has_value());
    std::span<std::byte> er_body = *er_r;

    EXPECT_FALSE(body_contains_tag(er_body, "8")) << "ExecRpt body contains 8=";
    EXPECT_FALSE(body_contains_tag(er_body, "9")) << "ExecRpt body contains 9=";
    EXPECT_FALSE(body_contains_tag(er_body, "34")) << "ExecRpt body contains 34=";
    EXPECT_FALSE(body_contains_tag(er_body, "49")) << "ExecRpt body contains 49=";
    EXPECT_FALSE(body_contains_tag(er_body, "52")) << "ExecRpt body contains 52=";
    EXPECT_FALSE(body_contains_tag(er_body, "56")) << "ExecRpt body contains 56=";
    EXPECT_FALSE(body_contains_tag(er_body, "10")) << "ExecRpt body contains 10=";

    // ── RC#1: SOH-injection counter-tests (gate-b/r1) ──────────────────────────
    // A string field containing SOH must be REJECTED by the builder so that no
    // injected boundary tag reaches the body.  We assert the builder returns
    // unexpected — the INV-2 guarantee is maintained because the build fails
    // before any bytes are written to the body.

    // NOS: cl_ord_id containing SOH + session-tag injection ("ORD\x0149=EVIL")
    {
        std::array<std::byte, kBufSize> inject_buf{};
        auto inject_r =
            fixpp::session::build_new_order_single(std::span<std::byte>{inject_buf},
                                                   "ORD\x01"
                                                   "49=EVIL",
                                                   "AAPL", '1', qty, px, "20240601-09:30:00");
        EXPECT_FALSE(inject_r.has_value())
            << "NOS cl_ord_id with SOH+session-tag injection must be rejected (RC#1)";
    }

    // NOS: symbol containing SOH + non-session-tag injection ("AAPL\x0158=x")
    {
        std::array<std::byte, kBufSize> inject_buf{};
        auto inject_r =
            fixpp::session::build_new_order_single(std::span<std::byte>{inject_buf}, "ORD1",
                                                   "AAPL\x01"
                                                   "58=x",
                                                   '1', qty, px, "20240601-09:30:00");
        EXPECT_FALSE(inject_r.has_value())
            << "NOS symbol with SOH injection must be rejected (RC#1)";
    }

    // ExecRpt: order_id with SOH + session-tag injection ("ORD\x0149=EVIL")
    {
        std::array<std::byte, kBufSize> inject_buf{};
        auto inject_r =
            fixpp::session::build_execution_report(std::span<std::byte>{inject_buf},
                                                   "ORD\x01"
                                                   "49=EVIL",
                                                   "EXEC1", 'F', '2', "AAPL", '2', lq, cq, ap);
        EXPECT_FALSE(inject_r.has_value())
            << "ExecRpt order_id with SOH+session-tag injection must be rejected (RC#1)";
    }

    // ExecRpt: exec_id with SOH + non-session-tag injection ("EX\x0158=x")
    {
        std::array<std::byte, kBufSize> inject_buf{};
        auto inject_r =
            fixpp::session::build_execution_report(std::span<std::byte>{inject_buf}, "ORD1",
                                                   "EX\x01"
                                                   "58=x",
                                                   'F', '2', "AAPL", '2', lq, cq, ap);
        EXPECT_FALSE(inject_r.has_value())
            << "ExecRpt exec_id with SOH injection must be rejected (RC#1)";
    }

    // ExecRpt: symbol with SOH injection ("AAPL\x0158=x")
    {
        std::array<std::byte, kBufSize> inject_buf{};
        auto inject_r = fixpp::session::build_execution_report(std::span<std::byte>{inject_buf},
                                                               "ORD1", "EXEC1", 'F', '2',
                                                               "AAPL\x01"
                                                               "58=x",
                                                               '2', lq, cq, ap);
        EXPECT_FALSE(inject_r.has_value())
            << "ExecRpt symbol with SOH injection must be rejected (RC#1)";
    }
}

// ── INV-3: numeric fidelity — value-equality, trailing-zero tolerant ─────────
// Build with "190.5" and verify the serialised form, when parsed back, equals
// decimal_t{"190.50"} by value — confirming INV-3 tolerance of trailing-zero forms.
TEST(BusinessMessagesBuild, Builder_NumericFidelity_DecimalValueEquality) {
    std::pmr::monotonic_buffer_resource arena{4096};

    // 190.5 and 190.50 must compare equal by decimal_t value
    auto d_1905 = make_decimal("190.5", &arena);
    auto d_19050 = make_decimal("190.50", &arena);
    EXPECT_EQ(d_1905, d_19050) << "Pre-condition: 190.5 == 190.50 by decimal_t value";

    auto qty = make_decimal("100", &arena);

    std::array<std::byte, kBufSize> buf{};
    auto r = fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "CLORD2", "GOOG",
                                                    '1', qty, d_1905, "20240601-12:00:00");
    ASSERT_TRUE(r.has_value());

    // Parse back the price field
    auto px_sv = extract_field_value(*r, "44");
    ASSERT_FALSE(px_sv.empty());
    auto px_bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(px_sv.data()), px_sv.size()};
    auto parsed_px = decimal_t::parse(px_bytes, &arena);
    ASSERT_TRUE(parsed_px.has_value());

    // Must compare equal to both 190.5 and 190.50
    EXPECT_EQ(*parsed_px, d_1905) << "Parsed price must == 190.5";
    EXPECT_EQ(*parsed_px, d_19050) << "Parsed price must == 190.50 (trailing-zero tolerance)";
}

// ── INV-4: fail-closed build atomicity ──────────────────────────────────────────
// Each invalid-input case returns unexpected AND leaves `out` byte-for-byte
// unchanged (atomicity guarantee: contracts/business-messages.md §Atomicity,
// data-model.md INV-4).
//
// RC#3 hardening (gate-b/r1): prefill `out` with sentinel 0xAB and assert the
// buffer is byte-for-byte unchanged after every failing call.
//
// RC#1 additions (gate-b/r1): SOH-injected string fields must be rejected.
TEST(BusinessMessagesBuild, Builder_InvalidField_NoUsableOutput) {
    std::pmr::monotonic_buffer_resource arena{4096};

    auto qty = make_decimal("100", &arena);
    auto px = make_decimal("190.5", &arena);
    auto zero = make_decimal("0", &arena);

    // RC#3: sentinel-prefilled main buffer and small buffer.
    std::array<std::byte, kBufSize> buf{};
    buf.fill(std::byte{0xABU});

    std::array<std::byte, 5> small_buf{};
    small_buf.fill(std::byte{0xABU});

    // Helper: assert buf is byte-for-byte the sentinel value after a failing call.
    // Captures buf by reference so callers need not pass it.
    auto assert_buf_unchanged = [&](const char* label) {
        SCOPED_TRACE(label);
        for (std::size_t i = 0; i < buf.size(); ++i) {
            if (buf[i] != std::byte{0xABU}) {
                ADD_FAILURE() << "buf[" << i << "] was modified on failure path "
                              << "(expected 0xAB, got 0x" << std::hex
                              << static_cast<unsigned>(buf[i]) << ")";
                return;
            }
        }
    };

    auto assert_small_unchanged = [&](const char* label) {
        SCOPED_TRACE(label);
        for (std::size_t i = 0; i < small_buf.size(); ++i) {
            if (small_buf[i] != std::byte{0xABU}) {
                ADD_FAILURE() << "small_buf[" << i << "] was modified on failure path";
                return;
            }
        }
    };

    // ── NOS invalid cases ──

    // Empty cl_ord_id
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "", "MSFT", '1',
                                                        qty, px, "20240101-10:00:00")
                     .has_value())
        << "Empty cl_ord_id must fail";
    assert_buf_unchanged("NOS empty cl_ord_id");

    // Empty symbol
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "", '1',
                                                        qty, px, "20240101-10:00:00")
                     .has_value())
        << "Empty symbol must fail";
    assert_buf_unchanged("NOS empty symbol");

    // RC#1: cl_ord_id with SOH + session-tag injection
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf},
                                                        "ORD\x01"
                                                        "49=EVIL",
                                                        "MSFT", '1', qty, px, "20240101-10:00:00")
                     .has_value())
        << "NOS cl_ord_id with SOH+49= injection must fail (RC#1)";
    assert_buf_unchanged("NOS cl_ord_id SOH-inject session-tag");

    // RC#1: symbol with SOH + non-session-tag injection
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1",
                                                        "AAPL\x01"
                                                        "58=x",
                                                        '1', qty, px, "20240101-10:00:00")
                     .has_value())
        << "NOS symbol with SOH injection must fail (RC#1)";
    assert_buf_unchanged("NOS symbol SOH-inject non-session-tag");

    // Out-of-range side (not '1' or '2')
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "MSFT",
                                                        '3', qty, px, "20240101-10:00:00")
                     .has_value())
        << "Side '3' must fail";
    assert_buf_unchanged("NOS side '3'");
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "MSFT",
                                                        '\0', qty, px, "20240101-10:00:00")
                     .has_value())
        << "Side NUL must fail";
    assert_buf_unchanged("NOS side NUL");

    // Ill-formed transact_time (too short)
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "MSFT",
                                                        '1', qty, px, "20240101")
                     .has_value())
        << "Truncated transact_time must fail";
    assert_buf_unchanged("NOS truncated transact_time");

    // Ill-formed transact_time (wrong length 18 chars — not 17 or 21)
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "ORD1", "MSFT",
                                                        '1', qty, px, "20240101-10:00:000")
                     .has_value())
        << "18-char transact_time must fail";
    assert_buf_unchanged("NOS 18-char transact_time");

    // Too-small output buffer (RC#3: also assert small_buf unchanged)
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{small_buf}, "ORD1",
                                                        "MSFT", '1', qty, px, "20240101-10:00:00")
                     .has_value())
        << "Too-small buffer must fail with wire_frame_too_large";
    assert_small_unchanged("NOS too-small buf");

    // ── ExecRpt invalid cases ──

    // Empty order_id
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "", "EXEC1", 'F',
                                                        '2', "MSFT", '1', zero, qty, px)
                     .has_value())
        << "Empty order_id must fail";
    assert_buf_unchanged("ExecRpt empty order_id");

    // Empty exec_id
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1", "", 'F',
                                                        '2', "MSFT", '1', zero, qty, px)
                     .has_value())
        << "Empty exec_id must fail";
    assert_buf_unchanged("ExecRpt empty exec_id");

    // Empty symbol
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1", "EXEC1",
                                                        'F', '2', "", '1', zero, qty, px)
                     .has_value())
        << "Empty symbol must fail";
    assert_buf_unchanged("ExecRpt empty symbol");

    // RC#1: order_id with SOH + session-tag injection
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf},
                                                        "ORD\x01"
                                                        "49=EVIL",
                                                        "EXEC1", 'F', '2', "MSFT", '1', zero, qty,
                                                        px)
                     .has_value())
        << "ExecRpt order_id with SOH+49= injection must fail (RC#1)";
    assert_buf_unchanged("ExecRpt order_id SOH-inject session-tag");

    // RC#1: exec_id with SOH + non-session-tag injection
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1",
                                                        "EX\x01"
                                                        "58=x",
                                                        'F', '2', "MSFT", '1', zero, qty, px)
                     .has_value())
        << "ExecRpt exec_id with SOH injection must fail (RC#1)";
    assert_buf_unchanged("ExecRpt exec_id SOH-inject non-session-tag");

    // RC#1: symbol with SOH injection
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1", "EXEC1",
                                                        'F', '2',
                                                        "AAPL\x01"
                                                        "58=x",
                                                        '1', zero, qty, px)
                     .has_value())
        << "ExecRpt symbol with SOH injection must fail (RC#1)";
    assert_buf_unchanged("ExecRpt symbol SOH-inject");

    // NUL exec_type (non-printable/control)
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1", "EXEC1",
                                                        '\0', '2', "MSFT", '1', zero, qty, px)
                     .has_value())
        << "NUL exec_type must fail";
    assert_buf_unchanged("ExecRpt NUL exec_type");

    // NUL ord_status
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1", "EXEC1",
                                                        'F', '\0', "MSFT", '1', zero, qty, px)
                     .has_value())
        << "NUL ord_status must fail";
    assert_buf_unchanged("ExecRpt NUL ord_status");

    // Out-of-range side for ExecRpt
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{buf}, "ORD1", "EXEC1",
                                                        'F', '2', "MSFT", '9', zero, qty, px)
                     .has_value())
        << "Side '9' must fail";
    assert_buf_unchanged("ExecRpt side '9'");

    // Too-small output buffer (RC#3: also assert small_buf unchanged)
    EXPECT_FALSE(fixpp::session::build_execution_report(std::span<std::byte>{small_buf}, "ORD1",
                                                        "EXEC1", 'F', '2', "MSFT", '1', zero, qty,
                                                        px)
                     .has_value())
        << "Too-small buffer must fail with wire_frame_too_large";
    assert_small_unchanged("ExecRpt too-small buf");
}

// ── FR-008: TransactTime length+shape validation ────────────────────────────────
// Valid: 17-char "YYYYMMDD-HH:MM:SS" and 21-char "YYYYMMDD-HH:MM:SS.sss".
// Invalid: wrong length, digits/separators in wrong positions.
TEST(BusinessMessagesBuild, Builder_FR008_TransactTime_ShapeRejection) {
    std::pmr::monotonic_buffer_resource arena{4096};

    auto qty = make_decimal("10", &arena);
    auto px = make_decimal("50", &arena);

    std::array<std::byte, kBufSize> buf{};

    // Valid 17-char form
    EXPECT_TRUE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "O1", "X", '1',
                                                       qty, px, "20240601-09:30:00")
                    .has_value())
        << "17-char UTCTimestamp must succeed";

    // Valid 21-char form (with milliseconds)
    EXPECT_TRUE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "O1", "X", '1',
                                                       qty, px, "20240601-09:30:00.123")
                    .has_value())
        << "21-char UTCTimestamp must succeed";

    // Too short
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "O1", "X", '1',
                                                        qty, px, "20240601-09:30:0")
                     .has_value())
        << "16-char timestamp must fail";

    // Too long (not 17 or 21)
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "O1", "X", '1',
                                                        qty, px, "20240601-09:30:00.12345")
                     .has_value())
        << "23-char timestamp must fail";

    // Wrong separator position: dash not at position 8
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "O1", "X", '1',
                                                        qty, px, "20240601X09:30:00")
                     .has_value())
        << "Non-dash at position 8 must fail";

    // Empty string
    EXPECT_FALSE(fixpp::session::build_new_order_single(std::span<std::byte>{buf}, "O1", "X", '1',
                                                        qty, px, "")
                     .has_value())
        << "Empty transact_time must fail";
}

// ── [const §VIII.5] alloc witness: zero global-new allocations on the build path
// Wraps both builder calls in a zero-alloc window. The global operator new
// overrides above count every heap allocation. The decimal_t::parse calls in
// THIS test allocate via mr (PMR arena) and do NOT go through global new, so
// they must not increment the counter either (monotonic_buffer_resource uses
// the arena, not global new for small allocations).
//
// We only zero-assert the BUILDER CALLS themselves, not make_decimal().
TEST(BusinessMessagesBuild, Builder_NoHeap_CountingResource) {
#if FIXPP_SANITIZER_REPLACES_NEW
    GTEST_SKIP() << "global operator new replacement is incompatible with ASan "
                    "(alloc-dealloc-mismatch) and TSan (multiple-definition of operator new); "
                    "the zero-alloc witness runs in debug/release/ubsan, with mallocnesia "
                    "LD_PRELOAD as the CI-tier cross-check";
#else
    std::pmr::monotonic_buffer_resource arena{4096};

    // Construct decimals BEFORE the zero-alloc window
    auto qty = make_decimal("200", &arena);
    auto px = make_decimal("55.25", &arena);
    auto lq = make_decimal("0", &arena);
    auto cq = make_decimal("200", &arena);
    auto ap = make_decimal("55.25", &arena);

    std::array<std::byte, kBufSize> nos_buf{};
    std::array<std::byte, kBufSize> er_buf{};

    // ── Zero-alloc window: only the builder calls ──
    long before = g_alloc_count.load(std::memory_order_relaxed);

    auto nos_r = fixpp::session::build_new_order_single(std::span<std::byte>{nos_buf}, "CLORD-HEAP",
                                                        "TSLA", '2', qty, px, "20240101-00:00:00");

    auto er_r = fixpp::session::build_execution_report(
        std::span<std::byte>{er_buf}, "ORD-HEAP", "EXEC-HEAP", 'F', '2', "TSLA", '2', lq, cq, ap);

    long after = g_alloc_count.load(std::memory_order_relaxed);
    // ── End zero-alloc window ──

    EXPECT_TRUE(nos_r.has_value()) << "NOS builder must succeed for alloc witness";
    EXPECT_TRUE(er_r.has_value()) << "ExecRpt builder must succeed for alloc witness";

    EXPECT_EQ(after, before)
        << "build_new_order_single + build_execution_report must not call global operator new "
           "(alloc delta = "
        << (after - before)
        << "); "
           "mallocnesia LD_PRELOAD is the CI-tier cross-check";
#endif  // FIXPP_SANITIZER_REPLACES_NEW
}

// ── INV-4 coverage: internal-scratch-cap overflow guard ─────────────────────────
// [061-typed-app-messages T013/T014 update] D/8 are now built on
// fixpp::wire::body_builder (include/fixpp/wire/body_builder.hpp), which
// accumulates fields into a pmr arena and checks the TOTAL serialized body
// against a single fixed internal cap (kBodyCap = 3800 B, body_builder.cpp,
// TU-local) once, at commit() — not incrementally per field against a
// 1024-byte scratch as the pre-061 hand-rolled wfield/wchar/wdecimal helpers
// did. The old "sweep the overflow point backward through the field
// sequence" narrative no longer applies (there is no per-field guard at
// these sizes); this test now pins the NEW single-shot cap directly:
//   - an oversized leading field pushes the TOTAL body past kBodyCap ->
//     fail-closed (wire_frame_too_large), `out` untouched (INV-4 atomicity).
//   - a large-but-under-cap field succeeds, proving the boundary is real
//     (not merely "any big input fails").
// `out` is sized > kBodyCap (4096) so the internal cap fires, not the
// out-too-small check.
//
// RC#3 hardening (gate-b/r1, preserved): `out` is sentinel-prefilled with
// 0xCD; the overflow case asserts the full buffer is unchanged, confirming
// the cap guard fired BEFORE any copy to `out`.
TEST(BusinessMessagesBuild, Builder_ScratchOverflow_PerFieldGuards) {
    std::pmr::monotonic_buffer_resource arena{4096};
    const auto qty = make_decimal("1", &arena);
    const auto px = make_decimal("1", &arena);
    const auto zero = make_decimal("0", &arena);

    std::array<std::byte, 4096> out{};

    // NOS body total ≈ cl_ord_id_len + 55; kBodyCap=3800 overflow when
    // total > 3800 (len >= 3746).
    for (std::size_t len : {3746U, 3760U, 3800U}) {
        out.fill(std::byte{0xCDU});
        const std::string big(len, 'A');
        const auto r = fixpp::session::build_new_order_single(std::span<std::byte>{out}, big, "S",
                                                              '1', qty, px, "20240101-10:00:00");
        EXPECT_FALSE(r.has_value())
            << "NOS cl_ord_id len=" << len << " must overflow the internal body cap and fail-closed";
        EXPECT_TRUE(
            std::all_of(out.begin(), out.end(), [](std::byte b) { return b == std::byte{0xCDU}; }))
            << "NOS overflow must not write to out (INV-4 atomicity), len=" << len;
    }
    // Under-cap large field: succeeds, proving the boundary above is real.
    {
        out.fill(std::byte{0xCDU});
        const std::string big(3000, 'A');
        const auto r = fixpp::session::build_new_order_single(std::span<std::byte>{out}, big, "S",
                                                              '1', qty, px, "20240101-10:00:00");
        EXPECT_TRUE(r.has_value()) << "NOS cl_ord_id len=3000 (under kBodyCap) must succeed";
    }

    // ExecRpt body total ≈ order_id_len + 50; kBodyCap=3800 overflow when
    // total > 3800 (len >= 3751).
    for (std::size_t len : {3751U, 3760U, 3800U}) {
        out.fill(std::byte{0xCDU});
        const std::string big(len, 'A');
        const auto r = fixpp::session::build_execution_report(std::span<std::byte>{out}, big, "E",
                                                              'F', '2', "S", '1', zero, qty, px);
        EXPECT_FALSE(r.has_value())
            << "ExecRpt order_id len=" << len
            << " must overflow the internal body cap and fail-closed";
        EXPECT_TRUE(
            std::all_of(out.begin(), out.end(), [](std::byte b) { return b == std::byte{0xCDU}; }))
            << "ExecRpt overflow must not write to out (INV-4 atomicity), len=" << len;
    }
    // Under-cap large field: succeeds, proving the boundary above is real.
    {
        out.fill(std::byte{0xCDU});
        const std::string big(3000, 'A');
        const auto r = fixpp::session::build_execution_report(std::span<std::byte>{out}, big, "E",
                                                              'F', '2', "S", '1', zero, qty, px);
        EXPECT_TRUE(r.has_value()) << "ExecRpt order_id len=3000 (under kBodyCap) must succeed";
    }
}
