#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/builder_validate.hpp
//
// 067-codegen-writer-emitter T024 [US3] — the GENERIC runtime
// required-presence walk (FR-006/contracts/generated-builder.md G5,
// data-model.md §1.3/§1.4). Header-only; recurses `writer_traits<T>`'s own
// level (top-level body OR one group-entry level) required-field checks,
// then every group child's per-occurrence entries, returning the
// pre-existing `error::wire_required_field_missing` (=38) on the FIRST
// missing required field / empty required group, else success. Off the
// serialize path (data-model.md §1.2/§1.3): `commit()`/`build_<Msg>` never
// calls this.
//
// `writer_traits<T>` is specialized per generated Args type (top-level
// `<Msg>Args` AND every nested group `<Msg>...Args`) in the GENERATED
// `Builders.hpp` (T025) — the required-tag data (from IR `FieldRef.rule`/
// `group_no_tag`) is baked into those specializations at codegen time; this
// header pulls NOTHING from `fixpp/dict` or the codegen tool at runtime — NO
// `wire->dict`/`wire->codegen` include edge ([arch §2.3] — `wire` may
// include `core`+`dictionary`, but this header deliberately does not, to
// keep the generic walk provably free of any dict/codegen dependency).
//
// Anchors: specs/067-codegen-writer-emitter/data-model.md §1.3/§1.4;
//          contracts/generated-builder.md G5.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <fixpp/core/error.hpp>

namespace fixpp::wire {

// One required top-level-body-or-group-entry SCALAR member of T. `tag` is
// carried for traceability (the presence table is codegen-derived per
// SC-004 — zero hand-authored required lists — not used in the walk logic
// itself); `is_present` is a codegen-emitted function pointer returning
// whether the corresponding `<Msg>...Args` optional member is engaged.
template <typename T>
struct required_check {
    std::uint16_t tag;
    bool (*is_present)(T const&) noexcept;
};

// One repeating-group child of T's own level. `required` mirrors the
// group's own dict rule (data-model.md §1.1): a REQUIRED group (plain
// `std::span`) must have `size() > 0`; an OPTIONAL group
// (`std::optional<std::span>`) that is `nullopt` is allowed and skipped
// entirely (G5/RC#2) — `count` returns `std::nullopt` for that case, else
// the number of present entries. `validate_entry` type-erases the recursive
// call into the child's own `writer_traits<ChildArgs>` via
// `validate_required<ChildArgs>`.
template <typename T>
struct group_check {
    bool required;
    std::optional<std::size_t> (*count)(T const&) noexcept;
    ::fixpp::core::expected_t<void> (*validate_entry)(T const&, std::size_t) noexcept;
};

// Primary template intentionally left undefined — every generated
// `<Msg>...Args` type gets an explicit specialization in `Builders.hpp`
// (T025). A missing specialization is a hard compile error at the generic
// call site below (incomplete type), never a silent no-op.
template <typename T>
struct writer_traits;

// The single generic recursive walk (FR-006/G5). Checks T's own required
// scalars first (fail-fast on the first missing one), then every group
// child: a REQUIRED group with `count()==0` fails; an OPTIONAL group that is
// `nullopt` (`count()==std::nullopt`) is skipped entirely; every PRESENT
// entry (required, or optional-and-engaged) is recursively validated.
template <typename T>
[[nodiscard]] ::fixpp::core::expected_t<void> validate_required(T const& args) noexcept {
    for (auto const& rc : writer_traits<T>::required_checks) {
        if (!rc.is_present(args)) {
            return ::std::unexpected(::fixpp::core::error::wire_required_field_missing);
        }
    }
    for (auto const& gc : writer_traits<T>::group_checks) {
        std::optional<std::size_t> const n = gc.count(args);
        if (!n.has_value()) {
            continue;  // optional group, nullopt: G5/RC#2 — skip entirely.
        }
        if (gc.required && *n == 0) {
            return ::std::unexpected(::fixpp::core::error::wire_required_field_missing);
        }
        for (std::size_t i = 0; i < *n; ++i) {
            auto r = gc.validate_entry(args, i);
            if (!r) {
                return r;
            }
        }
    }
    return {};
}

}  // namespace fixpp::wire
