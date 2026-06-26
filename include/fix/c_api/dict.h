/*
 * include/fix/c_api/dict.h — C-ABI dictionary loader (052 US1 / FR-001..003)
 *
 * [2i §3.10 / §4.2.1] commitment: expose fixpp_dict_t create/destroy so a pure-C
 * or Python consumer can construct the dictionary handle that
 * fixpp_session_config_set_dictionary requires.  Implements the L-050-1 seam
 * replacement — the "make the seam real" goal of 052.
 *
 * C-clean: no C++ symbols, no C++ syntax.  Compiles as C11.
 */

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
 * Wraps fixpp::dict::XmlLoader::load(path, std::pmr::get_default_resource()) and
 * produces an OWNING fixpp_dict_t (refcounted shared_ptr<const Dictionary>).  Pass
 * the result to fixpp_session_config_set_dictionary (which copies the shared_ptr),
 * then call fixpp_dict_destroy on your handle.
 *
 * THUNK: construction-time — catches all exceptions thrown by XmlLoader
 * (xml_parse_error / unknown_version_error / xml_oom_error on bad or missing
 * input) and maps them to FIXPP_ERR_CAPI_CONFIG_INVALID; no exception escapes
 * across extern "C".
 *
 * Reentrancy: single-thread.
 *
 * @return FIXPP_ERR_OK on success (*out_dict is non-null and owning).
 *         FIXPP_ERR_NULL_HANDLE if path or out_dict is NULL.
 *         FIXPP_ERR_CAPI_CONFIG_INVALID if the file is missing, unreadable, not
 *         well-formed XML, or not a recognised FIX dictionary version (*out_dict
 *         is set to NULL on this path).
 *         *out_dict is NULL on every failure path (FR-003).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_dict_load_from_xml(const char*     path,
                                                         fixpp_dict_t** out_dict);

/**
 * fixpp_dict_destroy — release the consumer's reference to a dictionary handle.
 *
 * NULL-safe (NULL input → no-op).  Never throws.  Owning/refcounted: releases
 * ONE shared_ptr reference; the underlying Dictionary persists while any session
 * still holds a reference to it.
 *
 * Idempotent double-destroy-safe via a tombstone ([2i §4.2.1]): checks tag_,
 * rewrites it to FIXPP_HANDLE_TAG_DEAD, and retains a dead shell (never freed)
 * so a second same-pointer destroy is a safe no-op (no double-free, no UAF).
 * Shell growth is O(load/destroy cycle count), NOT O(live handles) — load once
 * and reuse the handle where call frequency matters (see L-052-4).
 *
 * The full destroy critical section {tag_ check, shared_ptr release,
 * tag_=DEAD, dead-shell insert} runs under ONE process-global mutex so that
 * concurrent same-pointer destroys from two threads are serialised (FR-002).
 *
 * Reentrancy: thread-safe.
 */
FIXPP_API_EXPORT void fixpp_dict_destroy(fixpp_dict_t* dict);

#ifdef __cplusplus
}
#endif

#endif /* FIXPP_C_API_DICT_H */
