// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/emit_reify.cpp
//
// 003-dictionary-codegen — T032 — <vXX>/Reify.hpp: one owning_<Msg> class per
// typed message + the per-message owning_message_traits<Msg> specialisation +
// static_assert(std::is_same_v<dict::owning_message_t<Msg>,
//               fixpp::<vXX>::owning_<Msg>>) (data-model Entity 4; AC-G7a/AC-R2).
//
// owning_<Msg> shape (Entity 4 / I-9 / I-10):
//   * move-only (copy deleted); custom noexcept move ctor/assign (NOT =default)
//   * owns bytes_ (pmr::vector<std::byte>) + lazy view_cache_ (std::optional<MV>)
//   * same accessor surface as the flyweight (delegate through view())
//   * which() returns version_v (application_version static constexpr)
//   * move resets both source and destination view_cache_ to nullopt; moved-to
//     rebuilds view() against post-move bytes_.data() (AC-R4; seam #14)
//   * is_nothrow_move_constructible_v; no reference members (static_asserts)
//   * NOT sizeof==one pointer (it owns arena storage — Entity 4 vs Entity 1)
//
// 2b cutover (004 T059): from_view now performs the REAL owning deep-copy of
// src's validated frame bytes into the mr-owned bytes_; view() lazily rebuilds
// a MessageView<Index> over bytes_ via the Framer (the sole frame_view
// producer, [2b §4.2]). The behavioural reify round-trip (values survive a
// SOURCE-arena reset, AC-R4/SC-006) is GREEN. owning_<Msg>'s shape is
// unchanged (no carry_ member — a zero-cap local carry suffices). The
// owning_message_traits specialisation + static_assert compile as before
// (AC-G7a). NOTE: owning_message_handle's runtime-dispatch reify() path stays
// separately deferred (needs the build-tree _dispatch CMake target).
//
// Deterministic LF-only emission over the bytewise-sorted message list
// (NFR-003-7 / AC-T1). Uses the shared app_version_enum() / kMessageView
// from gen_util.hpp.
#include <cstdint>
#include <fixpp/dict/field_ref.hpp>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "emit.hpp"
#include "gen_util.hpp"
#include "ir.hpp"
#include "template_writer.hpp"

namespace fixpp::codegen {

namespace {

// kMessageView + app_version_enum() are the shared codegen helpers in
// gen_util.hpp (were verbatim-duplicated here and in emit_messages.cpp).

// Emit one scalar accessor body that delegates through the lazy view().
void emit_owning_scalar(TemplateWriter& w, std::string_view name, std::uint16_t tag, TypeKind k) {
    if (k == TypeKind::Decimal) {
        w.raw("    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::decimal_t>\n    ");
        w.raw(name);
        w.raw("(::std::pmr::memory_resource* mr) const noexcept\n");
        w.raw("    { auto fv = view().template get<");
        w.num(tag);
        w.raw(">(); if (!fv) return ::std::unexpected{fv.error()};\n");
        w.raw("      return ::fixpp::decimal_t::parse(fv->bytes(), mr); }");
        w.line();
        return;
    }

    std::string_view ct;
    bool lifetimebound = false;
    switch (k) {
        case TypeKind::String:
            ct = "::std::string_view";
            lifetimebound = true;
            break;
        case TypeKind::Char:
            ct = "char";
            break;
        case TypeKind::Bool:
            ct = "bool";
            break;
        case TypeKind::Int32:
            ct = "::std::int32_t";
            break;
        default:
            ct = "::std::string_view";
            lifetimebound = true;
            break;
    }
    w.raw("    [[nodiscard]] inline ::fixpp::core::expected_t<");
    w.raw(ct);
    w.raw(">\n    ");
    w.raw(name);
    w.raw("() const noexcept");
    if (lifetimebound) {
        w.raw(" [[clang::lifetimebound]]");
    }
    w.raw("\n    { return ::fixpp::dict::decode_field<");
    w.raw(ct);
    w.raw(">(view().template get<");
    w.num(tag);
    w.raw(">()); }");
    w.line();
}

// Emit one group accessor delegating through view().
// gq (qualified namespace prefix) and acc (accessor name) are semantically distinct;
// adjacent string_view params suppressed intentionally.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void emit_owning_group(TemplateWriter& w, std::string_view gq, std::string_view acc,
                       std::uint16_t no_tag) {
    w.raw("    [[nodiscard]] inline ::fixpp::wire::group_view<");
    w.raw(gq);
    w.raw("G_");
    w.num(no_tag);
    w.raw(">\n    ");
    w.raw(acc);
    w.raw("() const noexcept [[clang::lifetimebound]]\n");
    w.raw("    { return view().template group<");
    w.num(no_tag);
    w.raw(", ");
    w.raw(gq);
    w.raw("G_");
    w.num(no_tag);
    w.raw(">(); }");
    w.line();
}

// Emit one owning_<Msg> class definition + its out-of-line method bodies.
void emit_owning_message(TemplateWriter& w, VersionIR const& ir, std::string_view ns,
                         MessageIR const& m) {
    std::string const id = to_identifier(m.name);
    std::string const oid = "owning_" + id;

    // Groups namespace prefix.
    std::string gq;
    gq.reserve(40);
    gq += "::fixpp::";
    gq += ns;
    gq += "::groups::";

    // Collect top-level fields (group_no_tag == 0), deduped by tag.
    std::vector<FieldIR const*> const top = collect_top_fields(m);

    // ---- class body -------------------------------------------------------
    w.raw("class ");
    w.raw(oid);
    w.line(" {");
    w.line("public:");

    // Static metadata (same as flyweight).
    w.raw("    static constexpr ::std::string_view msg_type_v = \"");
    w.raw(m.msg_type);
    w.line("\";");
    w.raw("    static constexpr ::fixpp::dict::application_version version_v =\n");
    w.raw("        ::fixpp::dict::application_version::");
    w.raw(app_version_enum(ns));
    w.line(";");
    w.line();

    // Delete copy; declare custom noexcept move (NOT =default — I-9/AC-R4).
    w.raw("    ");
    w.raw(oid);
    w.raw("(");
    w.raw(oid);
    w.line(" const&) = delete;");
    w.raw("    ");
    w.raw(oid);
    w.raw("& operator=(");
    w.raw(oid);
    w.line(" const&) = delete;");
    w.raw("    ");
    w.raw(oid);
    w.raw("(");
    w.raw(oid);
    w.line(" &&) noexcept;");
    w.raw("    ");
    w.raw(oid);
    w.raw("& operator=(");
    w.raw(oid);
    w.line(" &&) noexcept;");
    w.raw("    ~");
    w.raw(oid);
    w.line("() = default;");
    w.line();

    // which() — returns version_v (AC-R2).
    w.line("    [[nodiscard]] static constexpr ::fixpp::dict::application_version");
    w.line("    which() noexcept { return version_v; }");
    w.line();

    // Named factory: from_view(src, mr) -> expected_t<owning_<Msg>>.
    w.raw("    [[nodiscard]] static ::fixpp::core::expected_t<");
    w.raw(oid);
    w.line(">");
    w.raw("    from_view(");
    w.raw(kMessageView);
    w.line(" const& src, ::std::pmr::memory_resource* mr);");
    w.line();

    // Lazy view() rebuilder.
    w.raw("    [[nodiscard]] ");
    w.raw(kMessageView);
    w.raw(" const&\n");
    w.line("    view() const noexcept [[clang::lifetimebound]];");
    w.line();

    // field_value(uint16_t) forwarder.
    w.line("    [[nodiscard]] inline ::fixpp::core::expected_t<::fixpp::wire::field_view>");
    w.line("    field_value(::std::uint16_t tag) const noexcept [[clang::lifetimebound]]");
    w.line("    { return view().get(tag); }");
    w.line();

    // Per-field accessors delegate through view().
    {
        std::unordered_set<std::string> used;
        auto uniq = [&](std::string base, std::uint16_t tag) -> std::string {
            return uniquify_accessor(used, std::move(base), tag);
        };
        for (auto const* f : top) {
            if (is_group_tag(ir, f->ref.tag)) {
                continue;
            }
            TypeKind const k = kind_of(f->ref.type);
            if (k == TypeKind::Skip) {
                continue;
            }
            emit_owning_scalar(w, uniq(to_accessor(f->name), f->ref.tag), f->ref.tag, k);
        }
        for (auto const* f : top) {
            if (!is_group_tag(ir, f->ref.tag)) {
                continue;
            }
            std::string const acc = uniq(to_accessor(strip_no_prefix(f->name)), f->ref.tag);
            emit_owning_group(w, gq, acc, f->ref.tag);
        }
    }

    // Private section.
    w.line("private:");
    // Explicit private ctor from mr — used by from_view().
    w.raw("    explicit ");
    w.raw(oid);
    w.line("(::std::pmr::memory_resource* mr) : bytes_(mr) {}");
    w.line();
    w.raw("    ::std::pmr::vector<::std::byte>                    bytes_;");
    w.line("      // owned arena copy");
    w.raw("    mutable ::std::optional<");
    w.raw(kMessageView);
    w.raw("> view_cache_;  // lazy, nullopt after move\n");
    w.line("};");
    w.line();

    // ---- out-of-line method bodies ----------------------------------------

    // Move constructor — custom, resets both sides' caches (I-9/AC-R4).
    w.raw("inline ");
    w.raw(oid);
    w.raw("::");
    w.raw(oid);
    w.raw("(");
    w.raw(oid);
    w.line(" && other) noexcept");
    w.line("    : bytes_(::std::move(other.bytes_))");
    w.line("    , view_cache_(::std::nullopt) {");
    w.line("    other.view_cache_ = ::std::nullopt;  // reset source cache (I-9/AC-R4)");
    w.line("}");
    w.line();

    // Move assignment.
    w.raw("inline ");
    w.raw(oid);
    w.raw("& ");
    w.raw(oid);
    w.raw("::operator=(");
    w.raw(oid);
    w.line(" && other) noexcept {");
    w.line("    if (this != &other) {");
    w.line("        bytes_            = ::std::move(other.bytes_);");
    w.line("        view_cache_       = ::std::nullopt;  // reset dest cache (I-9/AC-R4)");
    w.line("        other.view_cache_ = ::std::nullopt;  // reset source cache");
    w.line("    }");
    w.line("    return *this;");
    w.line("}");
    w.line();

    // view() lazy rebuild — 2b cutover (004 T059). Spec-faithful: [2b §4.2]
    // makes the Framer the SOLE frame_view producer. bytes_ is a byte-
    // identical copy of an already-Framer-validated frame; re-feed it. A
    // complete single frame fed with an EMPTY carry never appends to the
    // carry (src/wire/framer.cpp), so a zero-capacity local pmr_carry_buffer
    // performs NO allocation and is safe in this noexcept context.
    // MessageView<Index>'s ctor builds the OffsetTable dict-free + noexcept.
    // Lazy + cache-resetting move (I-9/AC-R4): rebuilds against post-move
    // bytes_.data(). On any framing failure (impossible for a valid copy)
    // fall back to a default MV (field-absent; memory-safe, never UB).
    w.raw("inline ");
    w.raw(kMessageView);
    w.raw(" const&\n");
    w.raw(oid);
    w.line("::view() const noexcept {");
    w.line("    if (!view_cache_) {");
    w.line("        ::fixpp::wire::pmr_carry_buffer carry{0, bytes_.get_allocator().resource()};");
    w.line("        ::fixpp::wire::Framer framer{};");
    w.line("        ::fixpp::wire::frame_view out_arr[1]{};");
    w.line("        auto framed = framer.feed(");
    w.line("            ::std::span<const ::std::byte>{bytes_.data(), bytes_.size()},");
    w.line("            carry, ::std::span<::fixpp::wire::frame_view>{out_arr, 1});");
    w.line("        if (framed && !framed->empty()) {");
    w.raw("            view_cache_.emplace((*framed)[0], bytes_.get_allocator().resource());");
    w.line();
    w.line("        } else {");
    w.line("            view_cache_.emplace();");
    w.line("        }");
    w.line("    }");
    w.line("    return *view_cache_;");
    w.line("}");
    w.line();

    // from_view factory — 2b cutover (004 T059). Real owning deep-copy:
    //   * src.bytes() is the full validated frame span ([2b §4.2] /
    //     parser.hpp MessageView View-base). Deep-copy it into bytes_ (the
    //     mr-owned arena), so the owning_<Msg> survives a SOURCE-arena reset
    //     (AC-R4 / SC-006). view() lazily rebuilds the MV over bytes_.
    //   * The pmr::vector copy IS the PMR allocation that keeps the OOM trap
    //     path live (AC-R7 / seam #16); bad_alloc → dict_reify_oom
    //     ([2a §4.2] trap shape).
    //   * NOTE owning_message_handle's runtime-dispatch reify() path is a
    //     SEPARATE concern (needs the build-tree _dispatch CMake target) and
    //     stays deferred — T059 completes the typed reify_as<Msg> / SC-006
    //     round-trip only.
    w.raw("inline ::fixpp::core::expected_t<");
    w.raw(oid);
    w.line(">");
    w.raw(oid);
    w.raw("::from_view(");
    w.raw(kMessageView);
    w.line(" const& src, ::std::pmr::memory_resource* mr) {");
    w.line("    try {");
    w.raw("        ");
    w.raw(oid);
    w.line(" owned{mr};");
    w.line("        auto const sb = src.bytes();  // full validated frame span");
    w.line("        owned.bytes_.assign(sb.begin(), sb.end());  // deep copy into mr arena");
    w.line("        return owned;  // move (custom noexcept move ctor)");
    w.line("    } catch (::std::bad_alloc const&) {");
    w.line("        return ::std::unexpected{::fixpp::core::error::dict_reify_oom};");
    w.line("    }");
    w.line("}");
    w.line();
}

}  // namespace

std::string emit_reify(VersionIR const& ir) {
    TemplateWriter w;

    // File header.
    w.line("// SPDX-License-Identifier: AGPL-3.0-or-later");
    w.line("// GENERATED by fixpp-codegen (003-dictionary-codegen, T032). DO NOT EDIT.");
    w.line("// <vXX>/Reify.hpp — owning_<Msg> arena-owned cross-strand holders ([2c §4.8];");
    w.line("// data-model Entity 4; AC-R2/AC-G7a). Shape oracle:");
    w.line("// specs/003-dictionary-codegen/contracts/generated_message.hpp.");
    w.line("//");
    w.line("// 2b cutover (004 T059): from_view deep-copies the validated frame into the");
    w.line("// mr-owned bytes_; view() lazily rebuilds a MessageView<Index> over bytes_");
    w.line("// via the Framer ([2b §4.2] sole producer). Values survive a SOURCE-arena");
    w.line("// reset (AC-R4/SC-006). owning_message_handle runtime dispatch stays deferred.");
    w.line("#pragma once");
    w.raw("#include <fixpp/");
    w.raw(ir.ns);
    w.line("/Messages.hpp>   // flyweight classes + owning_<Msg> forward-decls");
    w.line("#include <fixpp/core/decimal_alias.hpp>");
    w.line("#include <fixpp/core/error.hpp>");
    w.line("#include <fixpp/dict/field_traits.hpp>");
    w.line("#include <fixpp/dict/reify.hpp>         // owning_message_traits primary template");
    w.line("#include <fixpp/dict/version_profile.hpp>");
    w.line("#include <fixpp/wire/message_view_contract.hpp>");
    w.line("#include <fixpp/wire/parser.hpp>          // MessageView<Index>, Framer,");
    w.line("                                          // pmr_carry_buffer, frame_view (T059)");
    w.line("#include <cstddef>");
    w.line("#include <cstdint>");
    w.line("#include <memory_resource>");
    w.line("#include <new>");
    w.line("#include <optional>");
    w.line("#include <span>");
    w.line("#include <string_view>");
    w.line("#include <type_traits>");
    w.line("#include <vector>");
    w.line();

    // Open namespace fixpp::<ns>.
    w.raw("namespace fixpp::");
    w.raw(ir.ns);
    w.line(" {");
    w.line();
    w.line("// owning_<Msg> — arena-owned cross-strand holders. Move-only; custom noexcept");
    w.line("// move (NOT =default, I-9/AC-R4). Owns bytes_ (pmr::vector<std::byte>) +");
    w.line("// lazy view_cache_ (std::optional<MV>). Accessor surface mirrors the flyweight.");
    w.line();

    for (auto const& m : ir.messages) {
        emit_owning_message(w, ir, ir.ns, m);
    }

    w.raw("}  // namespace fixpp::");
    w.line(ir.ns);
    w.line();

    // owning_message_traits specialisations — outside fixpp::<ns> so they are
    // in the global namespace which encloses fixpp::dict (required by the
    // standard for partial/full specialisations).
    w.line("// owning_message_traits specialisations — AC-G7a / seam #18.");
    w.line("// Outside fixpp::<ns>: a specialisation of fixpp::dict::owning_message_traits");
    w.line("// must be declared in a namespace enclosing fixpp::dict (the global namespace).");
    for (auto const& m : ir.messages) {
        std::string const id = to_identifier(m.name);
        std::string const oid = "owning_" + id;
        w.line();
        w.raw("template <>\nstruct fixpp::dict::owning_message_traits<fixpp::");
        w.raw(ir.ns);
        w.raw("::");
        w.raw(id);
        w.line("> {");
        w.raw("    using type = fixpp::");
        w.raw(ir.ns);
        w.raw("::");
        w.raw(oid);
        w.line(";");
        w.line("};");
        w.raw("static_assert(::std::is_same_v<\n");
        w.raw("    fixpp::dict::owning_message_t<fixpp::");
        w.raw(ir.ns);
        w.raw("::");
        w.raw(id);
        w.raw(">,\n");
        w.raw("    fixpp::");
        w.raw(ir.ns);
        w.raw("::");
        w.raw(oid);
        w.line(">);  // AC-G7a");
    }

    return std::move(w).take();
}

}  // namespace fixpp::codegen
