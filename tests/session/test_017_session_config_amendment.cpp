// tests/session/test_017_session_config_amendment.cpp
//
// T013 — Regression test: assert SessionConfig::log_sink_override is GONE and
// logger_override / tracer_override are present and default-null.
//
// The compile-time assertions (via static_assert and the compile guard below)
// verify the removal at build time. The runtime tests verify the default values
// of the new fields on a default-constructed SessionConfig.
//
// Anchor: contracts/adjacent-amendments.md item 3/4 / [2d §4.5] / [2k §4.5].

#include <gtest/gtest.h>

#include <fixpp/session/session_config.hpp>

#include <memory>
#include <type_traits>

// ── Compile-time: log_sink_override is GONE ──────────────────────────────────
//
// The test itself constitutes the compile-fail assertion: if log_sink_override
// still exists, the line below will compile (SFINAE won't fire on a field
// access — we use a stricter check: confirm the field type does NOT appear).
//
// Implementation: we use a trait that checks for the existence of the field
// by attempting to take its address. If the field does NOT exist, substitution
// fails and the test still compiles (the struct member is simply not present).
// We verify absence at runtime by confirming the struct is still
// copy-constructible (smoke).

namespace {

// Static assertion: logger_override and tracer_override MUST exist as
// std::shared_ptr members (compile-time — confirmed by the type checks below).
static_assert(
    std::is_same_v<decltype(fixpp::session::SessionConfig::logger_override),
                   std::shared_ptr<fixpp::log::Logger>>,
    "SessionConfig::logger_override must be std::shared_ptr<fixpp::log::Logger>");

static_assert(
    std::is_same_v<decltype(fixpp::session::SessionConfig::tracer_override),
                   std::shared_ptr<fixpp::otel::TracerProvider>>,
    "SessionConfig::tracer_override must be std::shared_ptr<fixpp::otel::TracerProvider>");

// Static assertion: SessionConfig must remain copy-constructible
// ([const §FR-001 / 010 W-5]).
static_assert(std::is_copy_constructible_v<fixpp::session::SessionConfig>,
              "SessionConfig must remain copy-constructible after 017 T012 amendment");

// ── Detection idiom: a member's presence/absence as a compile-time fact ──────
// This is the REAL removal gate for T012 (the prior SUCCEED() placeholder
// measured nothing — [[feedback_fail_placeholder_red_test]]). `has_member_*`
// is true iff `cfg.<member>` is a well-formed expression for SessionConfig.
template <class T, class = void>
struct has_log_sink_override : std::false_type {};
template <class T>
struct has_log_sink_override<
    T, std::void_t<decltype(std::declval<T&>().log_sink_override)>>
    : std::true_type {};

template <class T, class = void>
struct has_logger_override : std::false_type {};
template <class T>
struct has_logger_override<
    T, std::void_t<decltype(std::declval<T&>().logger_override)>>
    : std::true_type {};

// T012 removal contract (contracts/adjacent-amendments.md item 4): the field is
// GONE. The self-check (logger_override detected PRESENT) proves the detector is
// not vacuously false — so the absence assertion is meaningful.
static_assert(!has_log_sink_override<fixpp::session::SessionConfig>::value,
              "SessionConfig::log_sink_override must be REMOVED (017 T012)");
static_assert(has_logger_override<fixpp::session::SessionConfig>::value,
              "detector self-check: logger_override must be detected present");

}  // namespace

// ── Runtime: new fields default to null ─────────────────────────────────────

TEST(SessionConfig017Amendment, LoggerOverrideDefaultNull) {
    fixpp::session::SessionConfig cfg{};
    EXPECT_EQ(cfg.logger_override, nullptr)
        << "SessionConfig::logger_override must default to nullptr";
}

TEST(SessionConfig017Amendment, TracerOverrideDefaultNull) {
    fixpp::session::SessionConfig cfg{};
    EXPECT_EQ(cfg.tracer_override, nullptr)
        << "SessionConfig::tracer_override must default to nullptr";
}

// ── Runtime: fields accept non-null assignments ──────────────────────────────
// (compile-level check that assignment compiles; value check is cosmetic)

TEST(SessionConfig017Amendment, LoggerOverrideAssignable) {
    fixpp::session::SessionConfig cfg{};
    // Assign a non-null shared_ptr<Logger> (can't construct Logger without
    // its full definition, but we can assign a nullptr and confirm the type).
    std::shared_ptr<fixpp::log::Logger> ptr{nullptr};
    cfg.logger_override = ptr;
    EXPECT_EQ(cfg.logger_override, nullptr);
}

TEST(SessionConfig017Amendment, TracerOverrideAssignable) {
    fixpp::session::SessionConfig cfg{};
    std::shared_ptr<fixpp::otel::TracerProvider> ptr{nullptr};
    cfg.tracer_override = ptr;
    EXPECT_EQ(cfg.tracer_override, nullptr);
}

// ── Compile-fail guard: log_sink_override must NOT exist ─────────────────────
//
// The definitive gate is the detection-idiom static_asserts in the anonymous
// namespace above: this TU fails to compile if log_sink_override is re-added.
// The runtime expectations below mirror that fact so the test reports
// meaningfully, and the detector is self-validated against logger_override
// (which MUST be detected present — otherwise the detector is vacuously false
// and the absence claim would be worthless).

TEST(SessionConfig017Amendment, LogSinkOverrideAbsent) {
    EXPECT_FALSE(has_log_sink_override<fixpp::session::SessionConfig>::value)
        << "SessionConfig::log_sink_override must be removed (017 T012, "
           "contracts/adjacent-amendments.md item 4)";
    // Self-validation: the detector correctly reports a field that DOES exist.
    EXPECT_TRUE(has_logger_override<fixpp::session::SessionConfig>::value)
        << "detector self-check failed — logger_override should be present, so "
           "the log_sink_override=absent result above is trustworthy";
}
