// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/alloc_guard/planted_entry_witness.cpp
//
// fixpp#497 — positive controls for the interceptor's hooks other than malloc, one entry
// point per run.
//
// planted_alloc_witness.cpp plants a malloc, so it vouches for the malloc hook only.
// This binary plants ONE allocation through the entry point named by the
// PLANTED_ENTRY environment variable, and each hooked entry point is registered
// as its own ctest entry (tests/alloc_guard/CMakeLists.txt), so a hook that stops
// counting reddens exactly the entry that names it. The interceptor is built without
// coverage instrumentation, so these entries are what shows that each hook runs.
//
//   aligned_new     an over-aligned operator new, which the C++ runtime serves through
//                   one of libc's aligned entry points rather than through malloc
//   calloc, aligned_alloc, posix_memalign, memalign, valloc, pvalloc
//                   a direct call to that entry point
//   realloc         a realloc of a block allocated BEFORE the window opens, so the
//                   realloc is the window's only allocation
//
// The entry point is chosen BEFORE the window opens, so the window holds only the
// planted call, which a different allocation therefore cannot stand in for.
//
// Same rules as planted_alloc_witness.cpp: not a gtest binary (the framework's own
// allocations would blur the count), and not WILL_FAIL (see tools/check_alloc.py's
// --expect-violation block). Linux-only, like the gates: memalign, valloc and pvalloc
// are glibc functions.

#include <malloc.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

#include "support/alloc_guard_markers.hpp"

namespace {

struct alignas(2 * __STDCPP_DEFAULT_NEW_ALIGNMENT__) over_aligned {
    std::byte bytes[2 * __STDCPP_DEFAULT_NEW_ALIGNMENT__];
};
static_assert(alignof(over_aligned) > __STDCPP_DEFAULT_NEW_ALIGNMENT__,
              "must select the align_val_t overload of operator new");

enum class plant {
    aligned_new,
    calloc,
    realloc,
    aligned_alloc,
    posix_memalign,
    memalign,
    valloc,
    pvalloc
};

bool select(const char* name, plant& out) {
    struct row {
        const char* name;
        plant which;
    };
    static constexpr row rows[] = {
        {.name = "aligned_new", .which = plant::aligned_new},
        {.name = "calloc", .which = plant::calloc},
        {.name = "realloc", .which = plant::realloc},
        {.name = "aligned_alloc", .which = plant::aligned_alloc},
        {.name = "posix_memalign", .which = plant::posix_memalign},
        {.name = "memalign", .which = plant::memalign},
        {.name = "valloc", .which = plant::valloc},
        {.name = "pvalloc", .which = plant::pvalloc},
    };
    for (const row& r : rows) {
        if (name != nullptr && std::strcmp(name, r.name) == 0) {
            out = r.which;
            return true;
        }
    }
    return false;
}

// Every pointer escapes through a volatile object, so no compiler may elide the
// allocation: an elided one would leave the window empty, and the control would pass
// while proving nothing.
//
// The raw allocation calls ARE the plant, so the ownership/RAII checks are off here, and
// so is concurrency-mt-unsafe (it lists valloc; this binary has one thread).
// NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc,concurrency-mt-unsafe)
void plant_one(plant which, void* before_window) {
    switch (which) {
        case plant::aligned_new: {
            over_aligned* volatile p = new over_aligned;
            delete p;
            break;
        }
        case plant::calloc: {
            void* volatile p = std::calloc(1, 64);
            std::free(p);
            break;
        }
        case plant::realloc: {
            void* volatile p = std::realloc(before_window, 4096);
            std::free(p);
            break;
        }
        case plant::aligned_alloc: {
            void* volatile p = std::aligned_alloc(64, 64);
            std::free(p);
            break;
        }
        case plant::posix_memalign: {
            void* q = nullptr;
            if (posix_memalign(&q, 64, 64) == 0) {
                void* volatile p = q;
                std::free(p);
            }
            break;
        }
        case plant::memalign: {
            void* volatile p = memalign(64, 64);
            std::free(p);
            break;
        }
        case plant::valloc: {
            void* volatile p = valloc(64);
            std::free(p);
            break;
        }
        case plant::pvalloc: {
            void* volatile p = pvalloc(64);
            std::free(p);
            break;
        }
    }
}
// NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc,concurrency-mt-unsafe)

}  // namespace

int main() {
    plant which{};
    // Return before the window: the wrapper then fails the control (its markers never
    // ran), so a misspelled or missing PLANTED_ENTRY fails rather than passing.
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded, read before any thread exists
    if (!select(std::getenv("PLANTED_ENTRY"), which)) return 2;

    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc) — realloc's plant needs a block
    void* before_window = which == plant::realloc ? std::malloc(8) : nullptr;

    if (alloc_guard_start) alloc_guard_start();
    plant_one(which, before_window);
    if (alloc_guard_end) alloc_guard_end();  // exits(1) under interception

    // Reached ONLY when nothing was intercepted; the wrapper reads exit 0 as the
    // control FAILING.
    return 0;
}
