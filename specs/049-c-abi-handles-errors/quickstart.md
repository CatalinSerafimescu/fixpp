# Quickstart — C ABI Feature A

A C consumer (no C++) integrating against the foundation surface this feature ships.

## Compatibility check + error strings (CA-002 / CA-004)

```c
#include <fix/c_api.h>     /* the single umbrella header — pulls error.h, version.h, handles.h, export.h */
#include <stdio.h>

int main(void) {
    fixpp_version_t v = fixpp_version();
    if (v.major != FIXPP_C_ABI_VERSION_MAJOR) {
        fprintf(stderr, "incompatible C ABI: engine %u.x, header %u.x\n",
                v.major, FIXPP_C_ABI_VERSION_MAJOR);
        return 1;                       /* hard incompatibility (major mismatch) */
    }
    printf("fixpp C ABI %u.%u.%u (library %u.%u.%u)\n",
           v.major, v.minor, v.patch,
           fixpp_library_version().major, fixpp_library_version().minor,
           fixpp_library_version().patch);

    /* Every fallible call returns a fixpp_error_t; turn any code into text: */
    printf("%s\n", fixpp_strerror(FIXPP_ERR_BUFFER_TOO_SMALL));   /* -> a stable description */
    printf("%s\n", fixpp_strerror(123456));                       /* -> "unknown error" (no crash) */
    return 0;
}
```

This is the SC-001 smoke: compiles as **C**, links only the C ABI, no C++ headers.

## Opaque handles (CA-001)

```c
fixpp_session_t* s = NULL;   /* opaque — definition is engine-internal */
/* create/operate functions arrive in Feature B/C. The catalogue + the
   null/invalid-handle codes (FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE)
   and the per-handle destroy/invalidation discipline (engine/dict/msg destroy;
   session closes; store invalidates) are fixed now. */
```

## What is NOT here yet
- Session create/connect/send/receive callbacks → **Feature B** (CA-005..007).
- Field get/set + repeating groups → **Feature C** (CA-008..010).
- `fixpp_engine_create` (records consumer minor for the error downgrade) → **Feature B** (L-049-1).
- Python bindings → blocked until Feature C.

## Build / verify (developer)
```bash
cd research/G19-fix-fpml-iso20022/library
# configure + build fixpp_capi (max -j2 per WSL2 cap), run the capi tests:
ctest --test-dir build/<preset> -R 'capi|error_surface|version|handles'
tools/check_capi_occupancy.sh   # occupancy/drift gate (Check A + Check B)
tools/check_capi_reentrancy.sh  # exactly-one reentrancy class per exported symbol
```
