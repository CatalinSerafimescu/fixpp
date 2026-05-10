/* bindings/python/fixpp.i — minimal SWIG interface for Phase 3
 * Wraps only fixpp_version_string() to wire the SWIG toolchain end-to-end.
 * Full C ABI binding surface lands in Phase 4+ per [arch §4.12].
 */

%module fixpp

%{
#include "fix/c_api.h"
%}

%include "fix/c_api.h"
