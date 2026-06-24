// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/capi/message_write.cpp — CA-009/CA-010-write: outbound construct/populate/commit + group builder
//
// Function bodies land per-story: T009 (US2 outbound scalar write) + T016 (US4 group build).

#include "fix/c_api/message.h"

#include "capi_internal.hpp"

// ── GroupInstance out-of-line definitions (T003 structural glue) ─────────────
//
// GroupInstance::fields is std::pmr::vector<AccumulatorEntry>; AccumulatorEntry
// must be COMPLETE at the point these bodies are compiled.  Including capi_internal.hpp
// above brings in the complete AccumulatorEntry definition (defined after GroupInstance
// in the header — both are visible to this TU).  The constructor/destructor/move are
// trivially delegating; no US2 logic here.

GroupInstance::GroupInstance(std::pmr::memory_resource* mr) : fields(mr) {}

GroupInstance::~GroupInstance() = default;

GroupInstance::GroupInstance(GroupInstance&&) noexcept = default;

GroupInstance& GroupInstance::operator=(GroupInstance&&) noexcept = default;
