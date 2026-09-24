# Contract C-1 — `body_builder` / `entry_handle` Data-field operation

Public C++ API, `include/fixpp/wire/body_builder.hpp`. The design rationale is in research.md R-1 to
R-4. Refusal codes are in data-model.md.

## Declarations (the shape to implement)

```cpp
namespace fixpp::wire {

class entry_handle {
public:
    // Appends <Length>=<octet count> then <data_tag>=<value>, both or neither.
    // The Length tag is the STANDARD pair's (core/length_data_pairs.hpp); a tag that only a
    // dictionary pairs is refused, whatever hooks the owning builder holds (FR-009).
    // `value` is copied verbatim; any octet 0x00-0xFF is accepted.
    // On failure the container is back at its pre-call size (INV-4); arena capacity the
    // failed call consumed is not reclaimed, as for every append.
    [[nodiscard]] fixpp::core::expected_t<void> set_data(
        std::uint16_t data_tag, std::span<const std::byte> value) noexcept;
    // ...existing members unchanged...
};

class body_builder {
public:
    // `hooks` is read by commit()'s pair check only (INV-6), so a hand-written dictionary
    // pair is checked there. Precondition: a bundle from `dict_hooks::for_table_view(tv)`
    // points at `tv`, which must outlive this builder (dict_hooks.hpp lifetime rule).
    explicit body_builder(std::string_view msg_type,
                          dict_hooks hooks = dict_hooks::none()) noexcept;

    [[nodiscard]] fixpp::core::expected_t<void> field_data(
        std::uint16_t data_tag, std::span<const std::byte> value) noexcept;
    // ...existing members unchanged; commit() additionally enforces INV-6...
};

}  // namespace fixpp::wire
```

`body_builder.hpp` gains `#include "dict_hooks.hpp"` (a wire-layer header that includes only
`length_data_pairs.hpp` and std headers). There is no dictionary include.

**Lifetime (Codex r1 P2-5).** `body_builder` stores `hooks` by value, and a `for_table_view` bundle
holds an unowned address of its `table_view`. The dictionary the hooks came from MUST outlive the
builder. `body_builder` is non-copyable, non-movable and meant as a scoped local, so the safe usage
is: build the hooks from a view that already outlives the scope, then construct the builder in that
scope.

## Behaviour clauses (each clause maps to at least one test)

**Oracle for every refusal and rollback clause (C-1.4, C-1.4b, C-1.5):** after the failed call, commit, and
require the body to equal byte-for-byte the body committed by an identical builder that never made
the call. `body_builder` exposes no size accessor, so this is the observable form of INV-4.

**Placement.** C-1.1–C-1.8 (including C-1.4b), C-1.10, C-1.11 go in `tests/wire/test_body_builder.cpp`
(`wire_body_builder_test`, links `fixpp_wire` only). C-1.9 needs a `table_view`, which links
`fixpp_dictionary` and allocates; it goes in `wire_dict_tests`, beside
`dict_hooks_custom_pair_test.cpp` as its outbound twin.

- **C-1.1 success.** `field_data(355, {0x41,0x01,0x42})` then `commit` yields a body containing
  `354=3<SOH>355=A<SOH>B<SOH>`, in that order, at the position of the call.
- **C-1.2 every octet.** For every `b` in `0x00–0xFF`, `field_data(355, {b})` commits, and a re-parse
  through the inbound parser recovers exactly `{b}` as tag 355. The same holds through `set_data` in
  a group entry. (SC-001's generated-builder witnesses are C-2.6.)
- **C-1.3 multi-digit Length.** A value of N octets (N ≥ 10, and a value near the body cap) emits
  `354=<N>` with no leading zeros.
- **C-1.4 refusals, on both surfaces.** Each case below runs twice: once through `field_data` at the
  top level, and once through `set_data` on a **live innermost entry** (a group opened, an entry
  added, and its delimiter field already set, so the instance is non-empty and delimiter-first and
  the no-call twin MUST itself commit OK; otherwise the byte-compare compares two INV-5 failures).
  Each is refused with its FR-004a code, and the commit-and-byte-compare oracle holds:
  - a non-Data tag: `11` with the value `"A\x01" "1=EVIL"` (a SOH-bearing payload: `body_builder`
    commit has no SOH scan, and the pair checker ignores a non-pair tag, so set-time resolution is
    the only guard) and `354` (a Length half) → `wire_unexpected_tag`; `8` (framing) →
    `wire_field_value_out_of_range`;
  - an empty value → `wire_field_value_out_of_range`.
  - (No runtime "framing Length" case: it is unreachable at set time; the property is a
    `static_assert` over the standard table, R-3.)
  The dictionary-only refusal is C-1.9's, on both surfaces too. The two surfaces are independent
  public members, so the stated symmetry (FR-002, R-3) is checked here, not assumed; this holds
  whether or not they share an append helper (R-3).
- **C-1.4b `set_data` handle checks.** Each of these returns `wire_invalid_field_format`, refused
  before any group or instance resolution (R-3 step 1, as `set_string`), and the
  commit-and-byte-compare oracle holds on the test's builder:
  - a default-constructed `entry_handle` (null owner);
  - the handle of an entry whose group was already closed by `group_end` (stale handle);
  - an outer entry's handle while a nested group's entry is innermost.
  Each is called with a valid standard Data tag and a non-empty value, so only the handle check can
  refuse it. The two named mutants in quickstart §3 (owner check omitted; innermost check omitted)
  must turn these RED.
- **C-1.5 rollback.**
  - **Arrangement:** the Length append must succeed and the Data allocation must fail. Pre-fill the
    arena with ordinary fields until the remaining capacity is below the Data value's size, then
    call `field_data` with a value larger than what remains but below `kBodyCap`.
  - **Arrangement witness:** a twin builder receiving the identical pre-fill sequence (the arena
    state is deterministic) MUST accept `field(354, std::int64_t{N})`, where N is the Data size — so
    the Length half fits and the failure is on the Data half. Assert this in the same test, on every
    platform (outer-vector regrowth differs by STL, so a Linux-only RED proof does not cover MSVC).
  - `field_data` returns `wire_frame_too_large`; the oracle holds (the stray Length would either
    make commit fail INV-6 or change the bytes).
  - **Nested twin (`set_data`).** The same arrangement inside a live innermost entry (non-empty,
    delimiter-first, as in C-1.4): the pre-fill ends in that entry, and the arrangement witness is
    the twin builder's `set_int(354, N)` on the same entry, which MUST succeed. `set_data` returns
    `wire_frame_too_large`, and the oracle holds: the committed body equals the no-call twin's,
    which itself commits OK, so the group instance is byte-identical.
  - An over-cap total at commit (`kBodyCap` exceeded, arena not exhausted) returns
    `wire_frame_too_large` with `out` untouched; a second commit returns the same error, since commit
    never mutates the accumulated contents.
- **C-1.6 INV-2 unchanged.** `SohInValue_RejectedBeforeAnyByteReachesOut` passes **byte-identical**
  to `origin/main`. `field(355, "A\x01B")` (the string path on a Data tag) is still refused by the
  content guard.
- **C-1.7 commit pair check (INV-6).** Each case commits with `wire_invalid_field_format` and `out`
  untouched:
  - `field(354, int 3)` with no following 355;
  - `field(355, "abc")` with no preceding 354;
  - `field(354, int 4)` + `field(355, "abc")`;
  - a group node between 354 and 355;
  - `field(354, "0")` + `field(355, "x")`;
  - a group whose `no_tag` is 354, with one instance, followed by a sibling `field(355, "x")` (one
    byte). This is refused because the group node is fed with an empty value (R-4). Feeding the
    count digits would accept it;
  - a group whose `no_tag` is 355.

  A hand-written, well-formed `field(354, int 3)` + `field(355, "abc")` commits.

  **Group-tag cases need a committing twin.** INV-5 and INV-6 both return
  `wire_invalid_field_format`, so a group-tag case passes for the wrong reason if its instance is
  empty or not delimiter-first. In every group-tag case (here and in C-1.9) each group instance is
  non-empty and delimiter-first, and each case has a twin that differs only as below and MUST
  commit, which proves INV-5 passes and the refusal is INV-6's:
  - "group node between 354 and 355": the same populated group placed after a well-formed
    `field(354, int 3)` + `field(355, "abc")`;
  - "group whose `no_tag` is 354 + sibling one-byte 355": the group's `no_tag` changed to a non-pair
    tag and the sibling turned into a well-formed pair (a bare sibling 355 fails INV-6 on its own);
  - "group whose `no_tag` is 355": the group's `no_tag` changed to a non-pair tag.
- **C-1.8 per-container.** The check runs separately in the top level and in each group instance. A
  Length at the end of one instance does not pair with a Data at the start of the next.
- **C-1.9 dictionary pairs (FR-009), in `wire_dict_tests`.** With a builder constructed from the
  hooks of a dictionary that declares a pair (5001, 5002):
  - `field_data(5002, "abc\x01" "1=EVIL")` is **refused** (`wire_unexpected_tag`), nothing appended
    (set time is standard-only); the same call through `set_data` on a live innermost entry
    (C-1.4's arrangement) is refused the same way, under the commit-and-byte-compare oracle;
  - a hand-written well-formed ASCII pair `field(5001, int 3)` + `field(5002, "abc")` **commits**;
  - a hand-written malformed one (`field(5001, int 4)` + `field(5002, "abc")`, or 5002 with no
    preceding 5001) is **refused at commit** (`wire_invalid_field_format`, `out` untouched);
  - the same malformed pair on a default (`none()`) builder commits, since 5001/5002 are plain tags
    there — this arm shows the hooks are what the commit check reads.
  - a group whose `no_tag` is the dictionary pair's Length (5001), with one instance, is **refused
    at commit** (`wire_invalid_field_format`, `out` untouched), because the checker reads the hooks'
    pairs for group nodes too (R-4); its committing twin (C-1.7) changes the `no_tag` to a non-pair
    tag;
  - A dictionary re-pairing a standard tag (95 → 5002) is ignored in favour of the standard pair
    (L-426-2), the same way `dict_hooks_custom_pair_test` exercises the inbound side.
- **C-1.10 Length-delimited entry.** An entry of a group whose `delimiter_tag` is 43109, populated by
  `set_data(42684, ...)`, commits (INV-5 holds because the Length node is first).
- **C-1.11 duplicate pair.** Calling `field_data(355, ...)` twice appends two pairs, the same
  append-not-upsert semantics `field()` has today. Both are well-formed, so commit accepts. This
  **diverges** from the C-ABI's `upsert_pair` (overwrite an adjacent pair, refuse a partial one); the
  divergence is intentional and disclosed in the B&L file (R-1).

## Non-goals

- No `std::string_view` overload (R-1).
- `body_builder` does not know FIX types. A caller may still `field_data` a Data tag the message does
  not declare; per-message membership stays the job of the generated builders and the validator.
