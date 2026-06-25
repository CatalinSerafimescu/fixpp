// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/dictionary_load_test.cpp — 052 US1 / SC-004
//
// Witness: fixpp_dict_load_from_xml + fixpp_dict_destroy public C-ABI (FR-001/FR-002/FR-003).
// Uses ONLY public headers — no capi_internal.hpp seam.
//
// Tests:
//   Positive  — load all four bundled XMLs; each usable in set_dictionary.
//   Negative  — NULL path/out_dict → NULL_HANDLE; missing path + malformed XML
//               → CAPI_CONFIG_INVALID + null out + non-empty strerror + no abort.
//   Tombstone — sequential double-destroy is a safe no-op (ASan discriminating).
//   TSan      — concurrent double-destroy witnesses the full-critical-section mutex
//               (FR-002 THREAD_SAFE discipline; data race absent under TSan).

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "fix/c_api/dict.h"
#include "fix/c_api/error.h"
#include "fix/c_api/session.h"  // fixpp_session_config_{create,destroy,set_dictionary}

#ifndef FIXPP_DICT_DIR
#  error "FIXPP_DICT_DIR must be set by CMake (target_compile_definitions)"
#endif

// ── Positive: load each bundled dictionary ────────────────────────────────────

TEST(DictLoadFromXml, LoadFix42) {
    fixpp_dict_t* d = nullptr;
    auto err = fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX42.xml", &d);
    ASSERT_EQ(err, FIXPP_ERR_OK);
    ASSERT_NE(d, nullptr);
    fixpp_dict_destroy(d);
}

TEST(DictLoadFromXml, LoadFix44) {
    fixpp_dict_t* d = nullptr;
    auto err = fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX44.xml", &d);
    ASSERT_EQ(err, FIXPP_ERR_OK);
    ASSERT_NE(d, nullptr);
    fixpp_dict_destroy(d);
}

TEST(DictLoadFromXml, LoadFix50Sp2) {
    fixpp_dict_t* d = nullptr;
    auto err = fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX50SP2.xml", &d);
    ASSERT_EQ(err, FIXPP_ERR_OK);
    ASSERT_NE(d, nullptr);
    fixpp_dict_destroy(d);
}

TEST(DictLoadFromXml, LoadFixt11) {
    fixpp_dict_t* d = nullptr;
    auto err = fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIXT11.xml", &d);
    ASSERT_EQ(err, FIXPP_ERR_OK);
    ASSERT_NE(d, nullptr);
    fixpp_dict_destroy(d);
}

// ── Positive: loaded dict is usable via fixpp_session_config_set_dictionary ───
//
// Witnesses FR-001: the shared_ptr is copied through the setter; the dict handle
// can be destroyed immediately after the setter returns (the config retains its
// own reference).

TEST(DictLoadFromXml, SetDictionaryRoundtrip) {
    fixpp_dict_t* d = nullptr;
    ASSERT_EQ(fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX44.xml", &d), FIXPP_ERR_OK);
    ASSERT_NE(d, nullptr);

    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    ASSERT_NE(cfg, nullptr);

    // set_dictionary copies the shared_ptr; dict handle can be destroyed immediately.
    EXPECT_EQ(fixpp_session_config_set_dictionary(cfg, d), FIXPP_ERR_OK);
    fixpp_dict_destroy(d);  // config still holds its own reference

    fixpp_session_config_destroy(cfg);
}

// ── Negative: NULL arguments → FIXPP_ERR_NULL_HANDLE ─────────────────────────

TEST(DictLoadFromXml, NullPathReturnsNullHandle) {
    fixpp_dict_t* d = nullptr;
    auto err = fixpp_dict_load_from_xml(nullptr, &d);
    EXPECT_EQ(err, FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(d, nullptr);  // *out_dict is null on every failure path (FR-003)
}

TEST(DictLoadFromXml, NullOutDictReturnsNullHandle) {
    // out_dict is null; cannot write *out_dict, but must not crash
    auto err = fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX42.xml", nullptr);
    EXPECT_EQ(err, FIXPP_ERR_NULL_HANDLE);
}

// ── Negative: non-existent path → FIXPP_ERR_CAPI_CONFIG_INVALID ──────────────

TEST(DictLoadFromXml, MissingFileReturnsConfigInvalid) {
    fixpp_dict_t* d = nullptr;
    auto err = fixpp_dict_load_from_xml("/nonexistent/path/that/does/not/exist.xml", &d);
    EXPECT_EQ(err, FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(d, nullptr);
    // fixpp_strerror must return a non-empty description (not "" or nullptr)
    const char* msg = fixpp_strerror(err);
    EXPECT_NE(msg, nullptr);
    EXPECT_GT(std::string_view{msg}.size(), 0u);
}

// ── Negative: syntactically malformed XML → FIXPP_ERR_CAPI_CONFIG_INVALID ─────

TEST(DictLoadFromXml, MalformedXmlReturnsConfigInvalid) {
    // Write garbage to a temp file; XmlLoader::load throws xml_parse_error.
    auto tmp = std::filesystem::temp_directory_path() /
               "fixpp_dict_load_malformed_test.xml";
    {
        std::ofstream f(tmp);
        ASSERT_TRUE(f.is_open()) << "could not create temp file " << tmp;
        f << "this is not xml at all <<<>>> malformed";
    }

    fixpp_dict_t* d = nullptr;
    auto err = fixpp_dict_load_from_xml(tmp.string().c_str(), &d);
    EXPECT_EQ(err, FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(d, nullptr);
    const char* msg = fixpp_strerror(err);
    EXPECT_NE(msg, nullptr);
    EXPECT_GT(std::string_view{msg}.size(), 0u);

    std::filesystem::remove(tmp);
}

// ── Tombstone: sequential double-destroy ─────────────────────────────────────
//
// Witnesses the [2i §4.2.1] tombstone discipline: the second destroy sees
// tag_==DEAD and is a safe no-op.  Discriminating under ASan: without the
// tombstone, the second destroy free()s already-freed memory → ASan reports
// heap-double-free.

TEST(DictDestroy, SequentialDoubleDestroyIsSafe) {
    fixpp_dict_t* d = nullptr;
    ASSERT_EQ(fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX42.xml", &d), FIXPP_ERR_OK);
    ASSERT_NE(d, nullptr);
    fixpp_dict_destroy(d);  // first: releases shared_ptr, rewrites tag_=DEAD
    fixpp_dict_destroy(d);  // second: sees tag_==DEAD → no-op (no double-free)
    // reaching here means no ASan double-free abort
}

TEST(DictDestroy, NullDestroyIsNoOp) {
    // NULL → no-op; must not crash.
    fixpp_dict_destroy(nullptr);
}

// ── TSan: concurrent double-destroy (FR-002 THREAD_SAFE / full-critical-section mutex)
//
// Two threads race fixpp_dict_destroy on the SAME handle.  The full-critical-
// section mutex (covering {tag_ check, dict.reset(), tag_=DEAD, shell push})
// serialises the two callers: exactly one thread does the teardown; the other
// sees tag_==DEAD and no-ops.  Discriminating under TSan: without the mutex,
// tag_ and the shared_ptr control block are concurrently written → TSan reports
// a data race.

TEST(DictDestroy, ConcurrentDoubleDestroyNoDataRace) {
    fixpp_dict_t* d = nullptr;
    ASSERT_EQ(fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX42.xml", &d), FIXPP_ERR_OK);
    ASSERT_NE(d, nullptr);

    // Release both threads at the same moment so they genuinely race.
    std::atomic<bool> go{false};
    auto destroy_fn = [&]() noexcept {
        while (!go.load(std::memory_order_acquire)) {
            /* spin */
        }
        fixpp_dict_destroy(d);
    };

    std::thread t1(destroy_fn);
    std::thread t2(destroy_fn);
    go.store(true, std::memory_order_release);
    t1.join();
    t2.join();
    // No crash (ASan) + no TSan data-race report → mutex covers the full critical section.
}
