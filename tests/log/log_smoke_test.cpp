// tests/log/log_smoke_test.cpp
//
// Slice 3a compile-time layout verification.
//
// This TU includes all three slice-3a headers and instantiates ArgValue/Record
// and a minimal Sink subclass so that:
//   - the static_asserts in record.hpp (sizeof == 24/256, trivially_copyable,
//     alignof == 64) are evaluated in this translation unit;
//   - the Level/Category/cat:: constants are used (ODR-use + compile check);
//   - the FIXPP_SLIT macro compiles;
//   - a concrete Sink subclass with all 4 pure-virtual overrides compiles.
//
// No behaviour is tested here — behaviour tests are slice 3b/3c (T014–T019).
// The "test" assertions below verify the invariants at runtime in a gtest TU
// (redundant with static_assert, but serves as a living executable gate).
#include <gtest/gtest.h>

#include <fixpp/log/level.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

// ── ArgValue layout ──────────────────────────────────────────────────────────

TEST(LogSlice3a, ArgValueSizeAndTriviality) {
    EXPECT_EQ(sizeof(fixpp::log::ArgValue), 24u);
    EXPECT_TRUE(std::is_trivially_copyable_v<fixpp::log::ArgValue>);
}

TEST(LogSlice3a, ArgValueFromU64) {
    auto av = fixpp::log::ArgValue::from_u64(42u);
    EXPECT_EQ(av.kind, fixpp::log::ArgValue::Kind::u64);
    EXPECT_EQ(av.u64, 42u);
}

TEST(LogSlice3a, ArgValueFromI64) {
    auto av = fixpp::log::ArgValue::from_i64(-7);
    EXPECT_EQ(av.kind, fixpp::log::ArgValue::Kind::i64);
    EXPECT_EQ(av.i64, -7);
}

TEST(LogSlice3a, ArgValueFromF64) {
    auto av = fixpp::log::ArgValue::from_f64(3.14);
    EXPECT_EQ(av.kind, fixpp::log::ArgValue::Kind::f64);
    EXPECT_DOUBLE_EQ(av.f64, 3.14);
}

TEST(LogSlice3a, ArgValueFromBool) {
    auto av = fixpp::log::ArgValue::from_bool(true);
    EXPECT_EQ(av.kind, fixpp::log::ArgValue::Kind::bool_val);
    EXPECT_TRUE(av.b);
}

TEST(LogSlice3a, ArgValueFromInlineShort) {
    auto av = fixpp::log::ArgValue::from_inline("hello");
    EXPECT_EQ(av.kind, fixpp::log::ArgValue::Kind::inline_str);
    EXPECT_EQ(av.inl.len, 5u);
    EXPECT_EQ(std::string_view(av.inl.data, av.inl.len), "hello");
}

TEST(LogSlice3a, ArgValueFromInlineTruncates) {
    // 16-character string — must be silently truncated to 15 bytes.
    auto av = fixpp::log::ArgValue::from_inline("1234567890123456");
    EXPECT_EQ(av.kind, fixpp::log::ArgValue::Kind::inline_str);
    EXPECT_EQ(av.inl.len, 15u);
}

TEST(LogSlice3a, ArgValueFromStatic) {
    static constexpr char literal[] = "fixpp";
    auto av = fixpp::log::ArgValue::from_static(literal);
    EXPECT_EQ(av.kind, fixpp::log::ArgValue::Kind::static_str);
    EXPECT_STREQ(av.static_ptr, "fixpp");
}

TEST(LogSlice3a, FIXPP_SLIT) {
    auto av = FIXPP_SLIT("hello");
    EXPECT_EQ(av.kind, fixpp::log::ArgValue::Kind::static_str);
    EXPECT_STREQ(av.static_ptr, "hello");
}

// ── Record layout ─────────────────────────────────────────────────────────────

TEST(LogSlice3a, RecordSizeAlignTriviality) {
    EXPECT_EQ(sizeof(fixpp::log::Record), 256u);
    EXPECT_EQ(alignof(fixpp::log::Record), 64u);
    EXPECT_TRUE(std::is_trivially_copyable_v<fixpp::log::Record>);
}

TEST(LogSlice3a, RecordDefaultConstruct) {
    // Default-constructed Record: trace_id/span_id zeroed; args default-init.
    fixpp::log::Record r{};
    EXPECT_EQ(r.span_id, 0u);
    for (auto b : r.trace_id) {
        EXPECT_EQ(b, 0u);
    }
    EXPECT_EQ(r.arg_count, 0u);
}

// ── Level and Category constants ──────────────────────────────────────────────

TEST(LogSlice3a, LevelValues) {
    using L = fixpp::log::Level;
    EXPECT_EQ(static_cast<uint8_t>(L::trace), 0u);
    EXPECT_EQ(static_cast<uint8_t>(L::debug), 1u);
    EXPECT_EQ(static_cast<uint8_t>(L::info),  2u);
    EXPECT_EQ(static_cast<uint8_t>(L::warn),  3u);
    EXPECT_EQ(static_cast<uint8_t>(L::error), 4u);
    EXPECT_EQ(static_cast<uint8_t>(L::fatal), 5u);
}

TEST(LogSlice3a, BuiltinCategoryValues) {
    using namespace fixpp::log::cat;
    EXPECT_EQ(session,   0x0001u);
    EXPECT_EQ(wire,      0x0002u);
    EXPECT_EQ(transport, 0x0003u);
    EXPECT_EQ(tls,       0x0004u);
    EXPECT_EQ(store,     0x0005u);
    EXPECT_EQ(otel,      0x0006u);
    EXPECT_EQ(control,   0x0007u);
    EXPECT_EQ(user,      0x0008u);
}

TEST(LogSlice3a, CategoryBitIndexMapping) {
    // Built-in categories 1..8 occupy bit indices 1..8 (category & 63u).
    using namespace fixpp::log::cat;
    EXPECT_EQ(session   & 63u, 1u);
    EXPECT_EQ(wire      & 63u, 2u);
    EXPECT_EQ(transport & 63u, 3u);
    EXPECT_EQ(tls       & 63u, 4u);
    EXPECT_EQ(store     & 63u, 5u);
    EXPECT_EQ(otel      & 63u, 6u);
    EXPECT_EQ(control   & 63u, 7u);
    EXPECT_EQ(user      & 63u, 8u);
}

TEST(LogSlice3a, FIXPP_LOG_CATEGORY_DoesNotCollide) {
    // "network" should produce a CRC32 whose low-6-bits are not in 1..8.
    // If this compiles (no static_assert failure), the collision check passed.
    constexpr auto my_cat = FIXPP_LOG_CATEGORY("network");
    EXPECT_NE(my_cat & 63u, 0u);  // non-zero bit index
    // Built-in range check: must NOT be in [1,8].
    auto idx = my_cat & 63u;
    bool collides = (idx >= 1u && idx <= 8u);
    EXPECT_FALSE(collides);
}

// ── Sink interface compiles with a minimal concrete subclass ──────────────────

namespace {

// Minimal concrete Sink that satisfies all 4 pure-virtual requirements.
// Used only to verify the interface compiles correctly.
class TestSink : public fixpp::log::Sink {
public:
    [[nodiscard]] fixpp::core::expected_t<void> open() override {
        return {};  // success
    }

    void emit(fixpp::log::Record const&) noexcept override {}

    void flush(std::chrono::milliseconds) noexcept override {}

    void close() noexcept override {}
};

}  // namespace

TEST(LogSlice3a, SinkSubclassCompiles) {
    TestSink s;
    auto result = s.open();
    EXPECT_TRUE(result.has_value());
}

TEST(LogSlice3a, SinkConfig) {
    fixpp::log::SinkConfig cfg;
    (void)cfg;
    SUCCEED();
}
