#ifndef FIXPP_CORE_SYNC_DETAIL_ATOMIC_SHARED_PTR_DETECT_HPP
#define FIXPP_CORE_SYNC_DETAIL_ATOMIC_SHARED_PTR_DETECT_HPP

// Integrated from the locked research harness
// (research/G19-fix-fpml-iso20022/atomic-shared-ptr/) — VERBATIM except this
// include-guard macro (relocated under core/sync/detail/). NFR-017 / feature
// 046. \internal — physically installed but excluded from the supported API /
// Doxygen stability surface ([arch §9.1]).

#include <version>

// Force macros are mutually exclusive to avoid ambiguous behavior.
#if defined(FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK) && \
    defined(FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE)
#error "FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK and FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE cannot both be defined."
#endif

// Conservative feature detection for the std atomic-shared-ptr primitive (P0718):
// - libc++: force fallback due hard-error instantiation behavior.
// - libstdc++ and MSVC-STL: native only when macro level is known-good.
// - unknown: fallback by default.
#ifndef FIXPP_HAS_STD_ATOMIC_SHARED_PTR
#if defined(FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
#define FIXPP_HAS_STD_ATOMIC_SHARED_PTR 0
#elif defined(FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE)
#define FIXPP_HAS_STD_ATOMIC_SHARED_PTR 1
#elif defined(_LIBCPP_VERSION)
#define FIXPP_HAS_STD_ATOMIC_SHARED_PTR 0
#elif defined(__GLIBCXX__) && defined(__cpp_lib_atomic_shared_ptr) && \
    (__cpp_lib_atomic_shared_ptr >= 201711L)
#define FIXPP_HAS_STD_ATOMIC_SHARED_PTR 1
#elif defined(_MSC_VER) && defined(__cpp_lib_atomic_shared_ptr) && \
    (__cpp_lib_atomic_shared_ptr >= 201711L)
#define FIXPP_HAS_STD_ATOMIC_SHARED_PTR 1
#else
#define FIXPP_HAS_STD_ATOMIC_SHARED_PTR 0
#endif
#endif

#endif  // FIXPP_CORE_SYNC_DETAIL_ATOMIC_SHARED_PTR_DETECT_HPP
