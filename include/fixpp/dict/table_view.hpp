// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/table_view.hpp
//
// `fixpp::dict::table_view` — production value type that `wire::Validator`
// and `wire::dictionary_driven_validator` bind BY VALUE to drive dictionary-
// backed inbound validation.
//
// This type is the Phase-1 realisation of the RC-A blocker identified in
// `specs/041-validation-gate-wiring/research.md §R-1`.
//
// CONTRACT (C-1, 041-validation-gate-wiring/contracts/validation-gate.md):
// The 6-method surface the validator calls:
//
//   bool         field_valid_for(string_view msg_type, uint16_t tag)    const noexcept
//   span<uint16_t const> required_fields(string_view msg_type)          const noexcept
//   uint16_t     group_first_field(uint16_t no_tag)                     const noexcept
//   span<uint16_t const> group_member_tags(uint16_t no_tag)             const noexcept
//   field_type   field_type_of(uint16_t tag)                            const noexcept
//   bool         enum_valid(uint16_t tag, span<const byte> value)       const noexcept
//       → 075: real dictionary-driven enum-domain check (FR-002/FR-003)
//
// STORAGE (E-2, data-model.md):
// Owns its tables using std::vector / std::unordered_map. Constructed ONCE at
// session/validator setup time by `Dictionary::as_table_view()` ([const §XV.1]
// — config-time, not per-message). Immutable after construction.
//
// INCLUDE-GRAPH CONSTRAINT ([const §XV.9]):
// This header is included (transitively) by `validator.hpp`, which lands on
// the `on_inbound_frame` co_await path. MUST NOT pull std::mutex,
// std::shared_mutex, or heavy asio into the closure. The dependency direction
// is:  dictionary.hpp → table_view.hpp  (never the reverse).
//
// Minimal includes only — deliberate:

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/field_type.hpp>  // field_type (7-value enum)
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fixpp::dict {

// Transparent hash for std::unordered_map<std::string, ...> enabling
// heterogeneous find(std::string_view) without constructing a std::string
// temporary. Both overloads hash via std::hash<std::string_view> so
// stored-string-key hashes remain consistent with string_view lookups.
// Combined with std::equal_to<> (transparent equality), this makes
// .find(string_view) fully allocation-free on the lookup path.
//
// [const §VIII.5 / §XV.1]: the two string-keyed maps (valid_, required_)
// are on the validate-ON hot path (called once per field per inbound message).
// Without this, a MsgType longer than SSO (~15 chars on libstdc++, ~22 on
// libc++) heap-allocates per message AND std::terminate()s on bad_alloc
// inside the noexcept methods.
struct string_hash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

// ── 063 Defect-A: context-scoped group-membership key ──────────────────────
// (msg_type, bounded parent-no_tag-path, no_tag) → {group_first, members}.
// data-model.md "GroupMembership" (Option A, Gate A round 1): a reused
// NumInGroup tag (e.g. FIX44 295, MassQuote's QuotEntryGrp vs
// QuotCxlEntriesGrp) resolves to the members it has AS THE MESSAGE USES IT,
// keyed by the FULL parent-no_tag chain (outermost first, no_tag itself
// excluded) + the owning msg_type (msg_type is mandatory: two message types
// can share the identical parent path and differ only by msg_type).
//
// kMaxGroupContextDepth mirrors wire::group_context's K=16
// (offset_table.hpp:239) NUMERICALLY only — this dict-layer header must not
// depend on the wire layer (dictionary.hpp -> table_view.hpp is the sole
// allowed edge; [const layer direction], never wire -> dict reversed here).
// A path deeper than K silently clamps (drops depth beyond 16) rather than
// growing unbounded; depth>=16 nesting is out of scope for 063 US1.
inline constexpr std::size_t kMaxGroupContextDepth = 16;

// Owned key, stored in table_view::group_ctx_.
struct group_ctx_key {
    std::string msg_type;
    std::array<std::uint16_t, kMaxGroupContextDepth> parent_path{};
    std::uint8_t depth = 0;
    std::uint16_t no_tag = 0;
};

// Non-owning query form — lets group_ctx_hash/group_ctx_equal drive a
// heterogeneous std::unordered_map::find() with ZERO allocation on the
// lookup path [pin#3-hash / const §VIII.5, §XV.1]: the caller passes a
// string_view + span, never constructing a temporary owned group_ctx_key.
struct group_ctx_query {
    std::string_view msg_type;
    std::span<std::uint16_t const> parent_path;
    std::uint16_t no_tag;
};

[[nodiscard]] inline std::size_t hash_group_ctx(std::string_view msg_type,
                                                std::span<std::uint16_t const> parent_path,
                                                std::uint16_t no_tag) noexcept {
    std::size_t h = std::hash<std::string_view>{}(msg_type);
    for (auto const t : parent_path) {
        h = (h * 1099511628211ULL) ^ t;
    }
    h = (h * 1099511628211ULL) ^ no_tag;
    return h;
}

struct group_ctx_hash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(group_ctx_key const& k) const noexcept {
        return hash_group_ctx(k.msg_type, {k.parent_path.data(), k.depth}, k.no_tag);
    }
    [[nodiscard]] std::size_t operator()(group_ctx_query const& q) const noexcept {
        return hash_group_ctx(q.msg_type, q.parent_path, q.no_tag);
    }
};

struct group_ctx_equal {
    using is_transparent = void;

    [[nodiscard]] static bool eq(std::string_view a_mt, std::span<std::uint16_t const> a_path,
                                 std::uint16_t a_no_tag, std::string_view b_mt,
                                 std::span<std::uint16_t const> b_path,
                                 std::uint16_t b_no_tag) noexcept {
        return a_no_tag == b_no_tag && a_mt == b_mt &&
               std::equal(a_path.begin(), a_path.end(), b_path.begin(), b_path.end());
    }
    [[nodiscard]] bool operator()(group_ctx_key const& a, group_ctx_key const& b) const noexcept {
        return eq(a.msg_type, {a.parent_path.data(), a.depth}, a.no_tag, b.msg_type,
                  {b.parent_path.data(), b.depth}, b.no_tag);
    }
    [[nodiscard]] bool operator()(group_ctx_key const& a, group_ctx_query const& b) const noexcept {
        return eq(a.msg_type, {a.parent_path.data(), a.depth}, a.no_tag, b.msg_type, b.parent_path,
                  b.no_tag);
    }
    [[nodiscard]] bool operator()(group_ctx_query const& a, group_ctx_key const& b) const noexcept {
        return eq(a.msg_type, a.parent_path, a.no_tag, b.msg_type, {b.parent_path.data(), b.depth},
                  b.no_tag);
    }
};

// Per-context group payload: the delimiter tag + the full member-tag list
// (declaration order; spans handed out by table_view alias this storage).
struct group_ctx_entry {
    std::uint16_t group_first = 0;
    std::vector<std::uint16_t> members;
};

// T017/T019 (data-model.md Entity B): enum-domain table entry. OWNS copies of
// the code bytes — does NOT alias the source Dictionary's name_pool_.
// as_table_view() may legally outlive the Dictionary it was built from
// (dictionary.hpp:193-205); aliasing would silently turn that into a
// use-after-free no existing test would catch (Complexity row 2).
struct enum_domain {
    std::vector<std::string> codes;  // sorted bytewise ascending, deduped
    bool multi_value{false};         // FR-005: MultiCharValue/MultiStringValue

    // Tier-2 single-char fast path (perf round): when EVERY declared code is
    // exactly one byte (69.7% of all codes across the 10 shipped dicts;
    // 1530 of 1894 enum fields are all-single-char), membership collapses to
    // one bit test in this 256-bit mask instead of a binary search over
    // `codes`. `all_single_char` starts true and is cleared by add_enum the
    // first time a code with size != 1 is inserted; when it is false the mask
    // is unused and `codes` (the byte-exact fallback) drives the check — so
    // mixed/multi-char sets (305 mixed + 59 all-multi fields) stay byte-exact.
    // `codes` is ALWAYS maintained (both representations of the same set) so
    // the fallback and the mutation witnesses are unaffected.
    bool all_single_char{true};
    std::array<std::uint64_t, 4> single_char_mask{};  // bit b set ⟺ 1-byte code b declared
};

[[nodiscard]] inline bool mask_has(std::array<std::uint64_t, 4> const& m,
                                   unsigned char b) noexcept {
    return ((m[static_cast<std::size_t>(b) >> 6U] >> (static_cast<std::size_t>(b) & 63U)) & 1U) !=
           0U;
}

[[nodiscard]] inline group_ctx_key make_group_ctx_key(std::string_view msg_type,
                                                      std::span<std::uint16_t const> parent_path,
                                                      std::uint16_t no_tag) {
    group_ctx_key key;
    key.msg_type = std::string{msg_type};
    key.no_tag = no_tag;
    key.depth =
        static_cast<std::uint8_t>(std::min<std::size_t>(parent_path.size(), kMaxGroupContextDepth));
    for (std::uint8_t i = 0; i < key.depth; ++i) {
        key.parent_path[i] = parent_path[i];
    }
    return key;
}

// Production value type exposing exactly the 6-method surface bound by
// `wire::dictionary_driven_validator`. Owns its backing storage; spans
// returned by the methods remain valid for the lifetime of this object.
//
// move-only (large owned tables; copying is intentionally deleted).
class table_view {
public:
    table_view() = default;
    ~table_view() = default;

    // Copy and move — both allowed. Copies duplicate the owned tables (used
    // when a single table_view configuration seeds multiple validator instances,
    // and by the mock-compatibility static_assert in validator_domain_test.cpp).
    // Move is noexcept; copy may throw on allocation failure.
    table_view(table_view const&) = default;
    table_view& operator=(table_view const&) = default;
    table_view(table_view&&) noexcept = default;
    table_view& operator=(table_view&&) noexcept = default;

    // ── 6-method validator surface (C-1) ────────────────────────────────────

    // True iff `tag` is declared for `msg_type` in the source Dictionary.
    [[nodiscard]] bool field_valid_for(std::string_view msg_type,
                                       std::uint16_t tag) const noexcept {
        auto it = valid_.find(msg_type);
        return it != valid_.end() && it->second.contains(tag);
    }

    // Hot-path hoist (validator perf round): the per-msg_type valid-tag set,
    // fetched ONCE per message so validate()'s per-field loop pays a uint16
    // set-membership test instead of re-hashing the loop-invariant msg_type
    // on every field. nullptr ⟺ msg_type unknown — field_valid_for would
    // return false for every tag, and the caller must treat nullptr the same
    // way. The pointer aliases storage owned by this table_view (stable for
    // its lifetime; the map is immutable after construction).
    [[nodiscard]] std::unordered_set<std::uint16_t> const* valid_tags_for(
        std::string_view msg_type) const noexcept {
        auto const it = valid_.find(msg_type);
        return it == valid_.end() ? nullptr : &it->second;
    }

    // Tags that are required for `msg_type`. Empty span if unknown.
    // The span aliases storage owned by this table_view (stable for lifetime).
    [[nodiscard]] std::span<std::uint16_t const> required_fields(
        std::string_view msg_type) const noexcept {
        auto it = required_.find(msg_type);
        if (it == required_.end()) {
            return {};
        }
        return {it->second.data(), it->second.size()};
    }

    // First-delimiter tag for group `no_tag`. Returns 0 if not a group.
    [[nodiscard]] std::uint16_t group_first_field(std::uint16_t no_tag) const noexcept {
        auto it = group_first_.find(no_tag);
        return it == group_first_.end() ? std::uint16_t{0} : it->second;
    }

    // All member tags for group `no_tag`. Empty span if not a group.
    // The span aliases storage owned by this table_view (stable for lifetime).
    [[nodiscard]] std::span<std::uint16_t const> group_member_tags(
        std::uint16_t no_tag) const noexcept {
        auto it = group_members_.find(no_tag);
        if (it == group_members_.end()) {
            return {};
        }
        return {it->second.data(), it->second.size()};
    }

    // ── 063 Defect-A: context-scoped group accessors ─────────────────────
    // `(msg_type, parent_path, no_tag)` overloads — the acceptance-gate
    // surface `group_member_fn_t` (parser.hpp) and `Validator::validate()`
    // call. Tries the context store FIRST; on a MISS falls back to the
    // LEGACY bare-`no_tag` store above.
    //
    // DUAL-STORE INVARIANT (design amendment #1, orchestrator-approved —
    // commit 0caafd23; tasks.md T013/T015). `Dictionary::as_table_view()`
    // populates BOTH the bare store and `group_ctx_`. The bare store is
    // REQUIRED, not a convenience: `Validator::group_first_field(no_tag)` is
    // a FROZEN pure-virtual (validator.hpp; `[const §XIV.2]` 5-virtual cap,
    // `[2b §4.6]` plugin interface), production-wired from `as_table_view()`
    // (session.cpp), and generic `Validator*` callers hold no parent-path
    // context — so it MUST answer from a context-free bare store. 063's
    // clarify-sanctioned public-surface change was scoped to
    // `group_member_fn_t` only, so widening this virtual was out of scope.
    // The 041-era `table_view` bare-accessor contract (witness:
    // tests/dictionary/table_view_test.cpp — real `as_table_view()`, bare
    // 1-arg calls, e.g. NoContraBrokers=382/NoPartyIDs=453) exercises the
    // same requirement.
    //
    // Consequence on a CONTEXT miss (a genuinely nested reused tag queried
    // with the wrong/root path — e.g. validator.hpp's non-nesting-aware
    // walk, L-063-3): this fallback returns the globally-first-seen variant
    // — exactly `main`'s pre-063 resolution (which had only the single
    // global-first-seen path everywhere), NOT a new failure mode. Defect A
    // stays FIXED wherever a caller supplies the exact context (the parser
    // lambda / `OffsetTable::group()`). Hand-built bare-API test fixtures
    // never populate `group_ctx_`, so they fall straight through to the
    // (unchanged) legacy bare lookup.
    [[nodiscard]] std::uint16_t group_first_field(std::string_view msg_type,
                                                  std::span<std::uint16_t const> parent_path,
                                                  std::uint16_t no_tag) const noexcept {
        // perf pre-filter (exact, not heuristic — see group_bits_ doc): a
        // clear bit proves BOTH stores miss this no_tag, so skip the
        // string+path hash of the context probe AND the bare probe entirely.
        // The validator's Step-3 walk calls this for EVERY offset-table
        // entry; on group-free traffic every call short-circuits here.
        if (!group_bit(no_tag)) {
            return 0;
        }
        auto const it = group_ctx_.find(group_ctx_query{msg_type, parent_path, no_tag});
        if (it != group_ctx_.end()) {
            return it->second.group_first;
        }
        return group_first_field(no_tag);  // legacy bare fallback — see doc above
    }

    [[nodiscard]] std::span<std::uint16_t const> group_member_tags(
        std::string_view msg_type, std::span<std::uint16_t const> parent_path,
        std::uint16_t no_tag) const noexcept {
        if (!group_bit(no_tag)) {  // same exact pre-filter as above
            return {};
        }
        auto const it = group_ctx_.find(group_ctx_query{msg_type, parent_path, no_tag});
        if (it != group_ctx_.end()) {
            return {it->second.members.data(), it->second.members.size()};
        }
        return group_member_tags(no_tag);  // legacy bare fallback — see doc above
    }

    // 7-value structural type category for `tag`. Defaults to String for
    // unknown tags (safe: the String arm imposes no structural constraint).
    [[nodiscard]] field_type field_type_of(std::uint16_t tag) const noexcept {
        auto it = types_.find(tag);
        return it == types_.end() ? field_type::String : it->second;
    }

    // T017 [FR-003/FR-004/FR-007/FR-008/FR-009/FR-014]: real enum-domain check.
    // `noexcept`, allocation-free on every path: no std::string materialization
    // of the wire value, no token vector — tokenization walks string_view
    // slices of the caller's buffer (`enum-domain.md` C-3).
    //
    // Two DISTINCT accept-floors — not the same rule, both required:
    [[nodiscard]] bool enum_valid(std::uint16_t tag,
                                  std::span<const std::byte> value) const noexcept {
        // perf pre-filter (exact — see enum_bits_ doc): a clear bit proves
        // enums_ has no entry for `tag`, which is Floor 1's accept arm, so
        // skip the hash probe entirely. Most fields of most messages are not
        // enum-backed; this makes them hash-free.
        if (!enum_bit(tag)) {
            return true;
        }
        auto const it = enums_.find(tag);

        // Floor 1 [FR-003]: tag absent from the enum store, OR its declared
        // code set is empty ⇒ ACCEPT. The anti-reject-everything floor — the
        // sole thing keeping FIXT11 working (its MsgType declares zero
        // <value> children).
        if (it == enums_.end() || it->second.codes.empty()) {
            return true;
        }

        // Floor 2 [FR-008]: an EMPTY field value bypasses the enum check
        // UNCONDITIONALLY — check_field_type decides instead. Distinct from
        // Floor 1: a literal byte-exact compare with no empty guard would
        // find "" absent from every codeset and reject via THIS arm, wrongly
        // flipping empty x String (e.g. ExecInst(18)=) from accept to reject
        // (contracts/enum-domain.md C-1 / DV-2). fixpp has no reason-4 slot,
        // so routing empty through the enum check would manufacture a 5-vs-4
        // divergence rather than achieve parity (FR-008 disposition (a)).
        if (value.empty()) {
            return true;
        }

        std::string_view const sv{reinterpret_cast<char const*>(value.data()), value.size()};
        enum_domain const& domain = it->second;

        if (!domain.multi_value) {
            // Single-value: byte-exact whole-token match. No case folding, no
            // prefix matching (FR-009) — MatchType(574)=A must reject on a
            // codeset declaring A1..A5 but not bare A.
            if (domain.all_single_char) {
                // Tier-2 fast path: every declared code is 1 byte, so any
                // value of length != 1 cannot match (whole-token), and a
                // 1-byte value is a single mask bit test. Byte-exact-identical
                // to code_declared over an all-1-byte sorted set.
                return sv.size() == 1 &&
                       mask_has(domain.single_char_mask, static_cast<unsigned char>(sv[0]));
            }
            return code_declared(domain.codes, sv);
        }

        // Multi-value [FR-004]: tokenize on a single space (T018), requiring
        // EVERY token be declared. An empty token (double/trailing space)
        // is never a declared code, so it rejects.
        std::size_t start = 0;
        while (start <= sv.size()) {
            std::string_view const token = next_token(sv, start);
            bool declared;
            if (domain.all_single_char) {
                // Same fast path per token. A multi-char token cannot exist as
                // a declared code in an all-single-char set (guard: size != 1
                // ⇒ reject), and an empty token (size 0) rejects — byte-exact
                // with the fallback's "empty token is never declared".
                declared = token.size() == 1 &&
                           mask_has(domain.single_char_mask, static_cast<unsigned char>(token[0]));
            } else {
                declared = code_declared(domain.codes, token);
            }
            if (!declared) {
                return false;
            }
        }
        return true;
    }

    // ── Build-time population surface ────────────────────────────────────────
    // Used by Dictionary::as_table_view() (non-chain void) and by test code
    // via the chain-style API below. Not part of the validator-facing contract.
    //
    // The chain-style methods mirror the test mock's builder surface so that
    // existing tests/wire/validator_*_test.cpp TUs can drop the mock include
    // and use this production type directly (RC-A closure, T009).

    void add_valid_tag(std::string_view msg_type, std::uint16_t tag) {
        valid_[std::string{msg_type}].insert(tag);
    }

    void add_required_tag(std::string_view msg_type, std::uint16_t tag) {
        required_[std::string{msg_type}].push_back(tag);
        valid_[std::string{msg_type}].insert(tag);
    }

    void set_field_type(std::uint16_t tag, field_type ft) { types_[tag] = ft; }

    // add_group_member returns *this for chaining (mirrors mock surface).
    table_view& add_group_member(std::uint16_t no_tag, std::uint16_t member_tag) {
        set_group_bit(no_tag);
        auto& members = group_members_[no_tag];
        for (auto const t : members) {
            if (t == member_tag) return *this;  // dedup
        }
        members.push_back(member_tag);
        return *this;
    }

    // Chain-style helpers — all return *this for fluent use.

    table_view& add_valid(std::string_view msg_type, std::uint16_t tag) {
        add_valid_tag(msg_type, tag);
        return *this;
    }

    table_view& add_required(std::string_view msg_type, std::uint16_t tag) {
        add_required_tag(msg_type, tag);
        return *this;
    }

    table_view& set_type(std::uint16_t tag, field_type ft) {
        set_field_type(tag, ft);
        return *this;
    }

    // set_group_first: sets the first-delimiter tag AND adds it as a member
    // (mirrors the mock's set_group_first behaviour exactly).
    table_view& set_group_first(std::uint16_t no_tag, std::uint16_t first) {
        set_group_bit(no_tag);
        group_first_[no_tag] = first;
        add_group_member(no_tag, first);
        return *this;
    }

    // Inserts an OWNED COPY of `value`'s bytes into `tag`'s code list (see
    // `enum_domain` above for why the table owns them). Sorted-on-insert and
    // deduped, so enum_valid's binary search is valid regardless of call order.
    table_view& add_enum(std::uint16_t tag, std::string_view value) {
        set_enum_bit(tag);
        auto& domain = enums_[tag];
        auto& codes = domain.codes;
        auto const pos = std::ranges::lower_bound(codes, value, code_less);
        if (pos == codes.end() || *pos != value) {
            codes.emplace(pos, value);  // std::string{value} — owned copy
        }
        // Tier-2 single-char mask maintenance (idempotent; safe on dup calls).
        // A 1-byte code sets its bit; ANY code with size != 1 (including the
        // empty string, were it ever inserted) permanently clears the
        // fast-path flag so the byte-exact `codes` fallback drives the check.
        if (value.size() == 1) {
            domain.single_char_mask[static_cast<std::size_t>(static_cast<unsigned char>(value[0])) >>
                                    6U] |=
                (std::uint64_t{1}
                 << (static_cast<std::size_t>(static_cast<unsigned char>(value[0])) & 63U));
        } else {
            domain.all_single_char = false;
        }
        return *this;
    }

    // T019 companion [FR-005]: the multi-value bit `add_enum` cannot itself
    // carry — without this, FR-004's tokenizer (T018) has no unit-level
    // witness that does not require a full XML load.
    table_view& set_multi_value(std::uint16_t tag, bool multi = true) {
        set_enum_bit(tag);
        enums_[tag].multi_value = multi;
        return *this;
    }

    // ── 063 Defect-A: context-scoped population surface ───────────────────
    // Used EXCLUSIVELY by Dictionary::as_table_view() (dictionary.cpp) — the
    // hardening invariant documented on the context-aware accessors above
    // depends on real dictionaries registering HERE and ONLY here (never via
    // add_group_member/set_group_first).
    void add_group_member_ctx(std::string_view msg_type, std::span<std::uint16_t const> parent_path,
                              std::uint16_t no_tag, std::uint16_t member_tag) {
        set_group_bit(no_tag);
        auto& entry = group_ctx_[make_group_ctx_key(msg_type, parent_path, no_tag)];
        for (auto const t : entry.members) {
            if (t == member_tag) return;  // dedup
        }
        entry.members.push_back(member_tag);
    }

    // Mirrors set_group_first's "sets the delimiter AND adds it as a member"
    // behaviour, context-scoped.
    void set_group_first_ctx(std::string_view msg_type, std::span<std::uint16_t const> parent_path,
                             std::uint16_t no_tag, std::uint16_t first) {
        set_group_bit(no_tag);
        group_ctx_[make_group_ctx_key(msg_type, parent_path, no_tag)].group_first = first;
        add_group_member_ctx(msg_type, parent_path, no_tag, first);
    }

private:
    // O(log C) byte-exact, whole-token lookup over a sorted code list — no
    // case folding, no prefix matching. `token` is a slice of the caller's
    // buffer; no temporary std::string is constructed.
    // Heterogeneous comparator: orders owned codes against a wire-value slice
    // without materializing a std::string (enum_valid is allocation-free).
    // Transparent so std::ranges::lower_bound accepts it in both directions.
    struct code_less_t {
        using is_transparent = void;
        [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a < b;
        }
    };
    static constexpr code_less_t code_less{};

    [[nodiscard]] static bool code_declared(std::vector<std::string> const& codes,
                                            std::string_view token) noexcept {
        auto const lb = std::ranges::lower_bound(codes, token, code_less);
        return lb != codes.end() && std::string_view{*lb} == token;
    }

    // T018 [FR-014]: locate the next single-space-delimited token in `value`
    // starting at `start`, and advance `start` past it. Empty tokens are NOT
    // skipped — a double space or a trailing space yields an empty token
    // (never a declared code, so it rejects). Byte-for-byte QuickFIX
    // (DataDictionary.h:265-275) — required for interop parity, not merely
    // permitted; do not invent a more forgiving tokenizer.
    [[nodiscard]] static std::string_view next_token(std::string_view value,
                                                     std::size_t& start) noexcept {
        auto const space_pos = value.find(' ', start);
        std::string_view const token = (space_pos == std::string_view::npos)
                                           ? value.substr(start)
                                           : value.substr(start, space_pos - start);
        start = (space_pos == std::string_view::npos) ? value.size() + 1 : space_pos + 1;
        return token;
    }

    // Valid-tag set per msg_type (used by field_valid_for).
    // transparent hash+equality: find(string_view) is allocation-free
    // [const §VIII.5 / §XV.1 — on the validate-ON hot path].
    std::unordered_map<std::string, std::unordered_set<std::uint16_t>, string_hash, std::equal_to<>>
        valid_;

    // Required-tag list per msg_type (insertion order preserved; spans stable).
    // transparent hash+equality: find(string_view) is allocation-free
    // [const §VIII.5 / §XV.1 — on the validate-ON hot path].
    std::unordered_map<std::string, std::vector<std::uint16_t>, string_hash, std::equal_to<>>
        required_;

    // Group first-delimiter (no_tag → first member tag).
    std::unordered_map<std::uint16_t, std::uint16_t> group_first_;

    // Group member-tag lists (no_tag → member tags; spans stable).
    std::unordered_map<std::uint16_t, std::vector<std::uint16_t>> group_members_;

    // perf pre-filter over BOTH group stores (bare + context): bit `no_tag`
    // is set by EVERY population path that can make a group accessor answer
    // non-empty for that no_tag (set_group_first / add_group_member /
    // set_group_first_ctx / add_group_member_ctx — all four, so the filter
    // is exact for ANY population pattern, including context-only test
    // fixtures and the dictionary.cpp members.front() fallback case where
    // the bare loop skips a no_tag the ctx loop registers). A clear bit ⟹
    // both stores miss ⟹ the accessors' current behaviour is already
    // "return 0 / empty span" — the filter only skips the (string+path)
    // hash probes, never changes an answer. 8 KiB, value-init to zero.
    // NOTE for future editors: any NEW population path into group_first_ /
    // group_members_ / group_ctx_ MUST call set_group_bit(no_tag).
    std::array<std::uint64_t, 1024> group_bits_{};

    [[nodiscard]] bool group_bit(std::uint16_t no_tag) const noexcept {
        return ((group_bits_[static_cast<std::size_t>(no_tag) >> 6U] >>
                 (static_cast<std::size_t>(no_tag) & 63U)) &
                1U) != 0U;
    }
    void set_group_bit(std::uint16_t no_tag) noexcept {
        group_bits_[static_cast<std::size_t>(no_tag) >> 6U] |=
            (std::uint64_t{1} << (static_cast<std::size_t>(no_tag) & 63U));
    }

    // 063 Defect-A context-scoped store: (msg_type, parent_path, no_tag) →
    // {group_first, members}. Populated EXCLUSIVELY by
    // Dictionary::as_table_view() via add_group_member_ctx/set_group_first_ctx
    // — see the hardening-invariant comment on the context-aware accessors
    // above. transparent hash+equality: find(group_ctx_query{...}) is
    // allocation-free on the lookup path [pin#3-hash / const §VIII.5, §XV.1].
    std::unordered_map<group_ctx_key, group_ctx_entry, group_ctx_hash, group_ctx_equal> group_ctx_;

    // Global tag → field_type map (built once from Dictionary).
    std::unordered_map<std::uint16_t, field_type> types_;

    // perf pre-filter over enums_ (same exact-superset pattern as
    // group_bits_ above): bit `tag` is set by BOTH population paths
    // (add_enum / set_multi_value). A clear bit ⟹ enums_.find(tag) misses
    // ⟹ enum_valid's Floor-1 accept — the filter only skips the hash
    // probe, never changes an answer. NOTE for future editors: any NEW
    // population path into enums_ MUST call set_enum_bit(tag).
    std::array<std::uint64_t, 1024> enum_bits_{};

    [[nodiscard]] bool enum_bit(std::uint16_t tag) const noexcept {
        return ((enum_bits_[static_cast<std::size_t>(tag) >> 6U] >>
                 (static_cast<std::size_t>(tag) & 63U)) &
                1U) != 0U;
    }
    void set_enum_bit(std::uint16_t tag) noexcept {
        enum_bits_[static_cast<std::size_t>(tag) >> 6U] |=
            (std::uint64_t{1} << (static_cast<std::size_t>(tag) & 63U));
    }

    // T017/T019 (data-model.md Entity B): enum-domain table, tag → owned
    // sorted/deduped code list + multi-value bit. Populated by add_enum /
    // set_multi_value (Dictionary::as_table_view()) or directly by tests.
    // Immutable after construction; enum_valid()'s sole backing store.
    std::unordered_map<std::uint16_t, enum_domain> enums_;
};

}  // namespace fixpp::dict
