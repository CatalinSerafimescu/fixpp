// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_usage_requirements.cpp
//
// 086 T010 (FR-009a(ii) / C-3 leg 3) — carrier TU for the usage-requirement
// probe target.
//
// The assertion is not in this file. It is the file(GENERATE) in
// tests/consumer/CMakeLists.txt that writes this target's effective
// COMPILE_DEFINITIONS / COMPILE_OPTIONS / COMPILE_FEATURES, and the read-back
// and compare performed by run_consumer_witness.cmake AFTER the sub-build — a
// file(GENERATE) nothing compares asserts nothing.
//
// Why the measurement cannot be taken from fixpp::capi's own property block
// (which quickstart §3 already diffs): compile definitions reach a C-ABI
// consumer through fixpp_capi_objects -> fixpp_log (src/capi/CMakeLists.txt:29-38)
// and never appear in fixpp::capi's block, which reads IDENTICALLY whether they
// propagate or not. Leg 2 is structurally blind to them; this target is where
// they become observable. Instrument measured in research.md R10.
//
// The TU only has to exist and compile: this target links fixpp::capi, so it
// must stay inside the isolated include interface.

#include <fix/c_api.h>
