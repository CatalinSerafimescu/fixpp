// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/capi/dictionary.cpp — fixpp_dict_load_from_xml + fixpp_dict_destroy
//                           (052 US1 / FR-001..003 / [2i §4.2.1])
//
// Two exported symbols:
//   fixpp_dict_load_from_xml — construction-time thunk: catches XmlLoader
//     exceptions → FIXPP_ERR_CAPI_CONFIG_INVALID; no exception crosses extern "C".
//   fixpp_dict_destroy — THREAD_SAFE via a full-critical-section process-global
//     mutex (FR-002): {tag_ check, dict.reset(), tag_=DEAD, dead-shell push}
//     run atomically under the lock so concurrent same-pointer destroys serialise.

#include <filesystem>
#include <memory_resource>
#include <mutex>
#include <vector>

#include "capi_internal.hpp"

#include "fix/c_api/dict.h"
#include "fix/c_api/error.h"

#include "fixpp/dict/xml_loader.hpp"

// ── Dead-shell registry (retained-shell tombstone, gate-b/r1 comment F4):
//    Each successful fixpp_dict_destroy pushes the shell pointer here so a
//    second same-pointer destroy can read tag_==DEAD and no-op safely (SC-004
//    idempotent double-destroy).  Growth is O(load/destroy cycle count), NOT
//    O(live dicts) — the shells are kept live in perpetuity so tag_ remains
//    readable after the dict's internals are freed.  A high-frequency consumer
//    should load a dictionary ONCE and reuse it; see L-052-4 in
//    spec/behaviors-and-limitations.md.  Heap-allocated once; never freed
//    (prevents ASan/LSan false-positives on retained shells that outlive the
//    process DSOcleanup order).
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static std::vector<fixpp_dict*>* s_dead_dict_shells  // NOLINT(cert-err58-cpp)
    = new std::vector<fixpp_dict*>();

// Process-global mutex covering the FULL destroy critical section so that
// concurrent same-pointer calls to fixpp_dict_destroy are data-race-free
// (FR-002 THREAD_SAFE discipline). A registry-only lock would leave tag_ and
// the shared_ptr control block exposed to a concurrent read-then-write race.
static std::mutex s_dict_destroy_mutex;  // NOLINT(cert-err58-cpp)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" {

fixpp_error_t fixpp_dict_load_from_xml(const char* path, fixpp_dict_t** out_dict) {
    // Null output pointer — cannot write *out_dict; return immediately.
    if (out_dict == nullptr) { return FIXPP_ERR_NULL_HANDLE; }
    // *out_dict is NULL on every failure path (FR-003).
    *out_dict = nullptr;
    if (path == nullptr) { return FIXPP_ERR_NULL_HANDLE; }

    // Construction-time thunk ([2i §5.2]): catch all exceptions; never let one
    // cross extern "C" (undefined behaviour in C callers; std::terminate for C++).
    try {
        fixpp::dict::XmlLoader loader;
        auto d = loader.load(std::filesystem::path{path},
                             std::pmr::get_default_resource());
        auto* h = new fixpp_dict{
            std::make_shared<const fixpp::dict::Dictionary>(std::move(d))};
        *out_dict = reinterpret_cast<fixpp_dict_t*>(h);
        return FIXPP_ERR_OK;
    } catch (...) {
        *out_dict = nullptr;
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
}

void fixpp_dict_destroy(fixpp_dict_t* dict) {
    if (dict == nullptr) { return; }
    auto* h = reinterpret_cast<fixpp_dict*>(dict);

    // Full-critical-section lock (FR-002): covers tag_ check, shared_ptr release,
    // tag_ rewrite, and dead-shell insert as ONE atomic unit.  This serialises two
    // threads racing the same-pointer destroy: the second sees tag_==DEAD and no-ops.
    std::unique_lock<std::mutex> lk(s_dict_destroy_mutex);

    if (h->tag_ == FIXPP_HANDLE_TAG_DEAD) { return; }  // already destroyed — safe no-op

    h->dict.reset();                        // release the consumer's shared_ptr reference
    h->tag_ = FIXPP_HANDLE_TAG_DEAD;        // tombstone (tag_ now unambiguously dead)
    s_dead_dict_shells->push_back(h);       // retain shell (bounded; see SHELL RETAIN note)
    // lk releases at end of scope
}

}  // extern "C"
