# Contract: dictionary loader (GAP-001 / FR-001..003)

New public header `include/fix/c_api/dict.h` (C11-clean — the `[2i]`-reserved name, line 281 / §3.10
commitment 5; it **coexists** in the same header with the 2c-owned `FIXPP_APPL_VER_*` constants, no
collision, so the header name is NO `[2i]` deviation). Implements the `[2i §2]` commitment-2 / §4.2-table
symbol that was specified but never built. "Make the L-050-1 seam real." Aggregated by the umbrella
`include/fix/c_api.h` (FR-014).

```c
#ifndef FIXPP_C_API_DICT_H
#define FIXPP_C_API_DICT_H

#include <fix/c_api/error.h>
#include <fix/c_api/export.h>
#include <fix/c_api/handles.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * fixpp_dict_load_from_xml — load a FIX XML data dictionary from a filesystem path.
 *
 * Wraps fixpp::dict::XmlLoader::load(path, std::pmr::get_default_resource()) and produces an OWNING
 * fixpp_dict_t (refcounted shared_ptr<const Dictionary>). Pass the result to
 * fixpp_session_config_set_dictionary (which copies the shared_ptr), then fixpp_dict_destroy your handle.
 *
 * THUNK: construction-time — catches std::exception (XmlLoader throws xml_parse_error /
 * unknown_version_error / xml_oom_error on bad input) → FIXPP_ERR_CAPI_CONFIG_INVALID; never lets an
 * exception cross extern "C". Reentrancy: SINGLE_THREAD.
 *
 * @return FIXPP_ERR_OK + non-null *out_dict; FIXPP_ERR_CAPI_CONFIG_INVALID (bad/missing/malformed XML,
 *         *out_dict=NULL); FIXPP_ERR_NULL_HANDLE (NULL path or out_dict). *out_dict is NULL on every
 *         failure path.
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_dict_load_from_xml(const char* path, fixpp_dict_t** out_dict);

/**
 * fixpp_dict_destroy — release the consumer's reference to a dictionary handle.
 *
 * NULL-safe (NULL → no-op), never-throws. Owning/refcounted: releases ONE shared_ptr reference; the
 * underlying Dictionary persists while any session still references it. Idempotent double-destroy-safe
 * via a TOMBSTONE (per the [2i §4.2.1] owning-handle discipline): checks the handle's tag_, rewrites it
 * to FIXPP_HANDLE_TAG_DEAD, retains a bounded dead shell, so a second same-pointer destroy is a safe
 * no-op. The tombstone MECHANISM mirrors fixpp_engine_destroy (tag->DEAD + retained shell), but the full
 * destroy critical section {tag_ check, shared_ptr release, tag_=DEAD, dead-shell insert} runs under one
 * process-global lock (NOT a registry-only lock) and this symbol stays THREAD_SAFE because [2i §4.2.1]
 * (line 415) mandates every *_destroy be thread-safe — whereas fixpp_engine_destroy's registry is
 * unsynchronized under its as-built
 * SINGLE_THREAD annotation. NOT the single-destroy-only fixpp_msg_destroy discipline. Reentrancy: THREAD_SAFE.
 */
FIXPP_API_EXPORT void fixpp_dict_destroy(fixpp_dict_t* dict);

#ifdef __cplusplus
}
#endif
#endif /* FIXPP_C_API_DICT_H */
```

**Implementation notes (src/capi/dictionary.cpp, NEW):**
- `fixpp_dict_load_from_xml`: NULL-check → `guarded_call_construction(FIXPP_ERR_CAPI_CONFIG_INVALID, …)`
  body: `fixpp::dict::XmlLoader loader; auto d = loader.load(std::filesystem::path{path}, std::pmr::get_default_resource());`
  → `auto h = new fixpp_dict{ std::make_shared<const fixpp::dict::Dictionary>(std::move(d)) };` →
  `*out_dict = reinterpret_cast<fixpp_dict_t*>(h);`. (Mirror the seam's `make_test_dict_handle`, but
  `XmlLoader::load(path)` instead of `make_minimal_dictionary()`.)
- `fixpp_dict_destroy`: NULL-safe; **tombstone discipline mirroring `fixpp_engine_destroy`'s mechanism**
  (`engine.cpp:289-371` — tag→DEAD + retained shell) but with a **wider lock than the engine's**. Because
  this symbol is `FIXPP_THREAD_SAFE` (per `[2i §4.2.1]` line 415) while `fixpp_engine_destroy` is the
  accepted-deviation `SINGLE_THREAD`, a process-global mutex (the same primitive guarding the dead-shell
  registry) MUST cover the **entire critical section as one atomic unit**:
  `{ check tag_ != FIXPP_HANDLE_TAG_DEAD → release the shared_ptr ref (dict.reset()) → rewrite tag_ =
  FIXPP_HANDLE_TAG_DEAD → insert the shell into the bounded dead-shell registry }`. A registry-only lock
  leaves a race on the **non-atomic `tag_` and `shared_ptr`, which sit OUTSIDE the registry** — two threads
  could each read `tag_ != DEAD`, both `dict.reset()` (control-block double-decrement / UAF), both
  tombstone, both insert. Holding the lock over the full sequence serializes concurrent same-pointer
  destroy: the second caller sees `tag_ == DEAD` under the lock and no-ops. The synchronization primitive
  itself is deferred to implement (a `.cpp`-TU mutex is §XV.9-consistent — the no-std::mutex rule is scoped
  to awaitable/coroutine headers); only the **locked-section boundary** is pinned here. Do **NOT**
  bare-`delete`, and do **NOT** copy the engine's registry-only (unsynchronized) locking (a double-free /
  post-destroy UAF on a second same-pointer destroy — the 050 Gate B / L-050-z tombstone-race class).
- **Signature-convention deviation (FR-001a):** the `[2i §2]` v0.1 sketch wrote `… → fixpp_dict_t*`;
  this adopts the as-built `fixpp_error_t`-return + out-param convention (every shipped 049/050/051
  symbol). Recorded LOCAL deviation; `[2i]` NOT edited.

**Witness (US1 / SC-004):** `tests/capi/dictionary_load_test.cpp` — load each bundled `dictionaries/FIX*.xml`
(OK + usable in `set_dictionary`); a missing path + a syntactically-malformed XML → `CAPI_CONFIG_INVALID`
+ non-empty `fixpp_strerror` + no abort; NULL path/out → `NULL_HANDLE`; **double-destroy safe** (the
second `fixpp_dict_destroy` on the same handle no-ops via the tombstone — no double-free/UAF); a **TSan
concurrent-double-destroy witness** — two threads racing `fixpp_dict_destroy` on the **same** handle,
asserting no data race on `tag_`/the `shared_ptr` and exactly-once teardown (the full-critical-section lock).
