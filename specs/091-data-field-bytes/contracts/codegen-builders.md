# Contract C-2 — Generated builders

Emitter: `tools/codegen/fixpp-codegen/emit_builders.cpp` (and `gen_util.hpp` comments). The design
is in research.md R-5 to R-8.

## C-2.1 Coupled Length+Data member (FR-010, FR-011)

For each coupled `LevelItem` (Length tag `L`, Data tag `D`, accessor `m`), in both arms:

```cpp
// top level (owner "bb")
if (args.m) {
    static_assert(::fixpp::wire::dict_hooks::none().length_tag_for_data(D) == L);
    auto r_pair = bb.field_data(D, ::std::as_bytes(::std::span{*args.m}));
    if (!r_pair) return ::std::unexpected(r_pair.error());
}
// nested (owner = the current entry_handle local)
if (item.m) {
    static_assert(::fixpp::wire::dict_hooks::none().length_tag_for_data(D) == L);
    auto r_pair = ehN.set_data(D, ::std::as_bytes(::std::span{*item.m}));
    if (!r_pair) return ::std::unexpected(r_pair.error());
}
```

- The Args member keeps the type `std::optional<std::string_view>`.
- No `field(L, …)` / `set_int(L, …)` is emitted for a coupled item.
- The token `r_data` no longer appears in generated output.

## C-2.2 Census (FR-012), and proof that it can fire

1. **Compile-time.** The `static_assert` above is emitted at every coupled call site.
   - **Positive control:** an emitter mutant passing `item.tag` for `D` must fail to compile the v44
     builders. Run it in a scratch copy, never in the PR worktree.
2. **Count.** Over the regenerated 078 goldens:
   - `grep -rl r_data` = 0;
   - per-version `grep -rlE 'field_data\(|set_data\('` = 74 / 154 / 286 / 320 (re-derive against
     the pre-change `r_data` counts; the two must be equal).
   - **Positive control:** the same script on the **pre-change** goldens reports the 834 `r_data`
     files as remaining, i.e. RED.

## C-2.3 `message_encoding` (FR-011a)

- **Which messages.** Those where any member at any depth is the Data half of a pair whose field name
  begins with `Encoded`. The set is derived from the IR, not hand-listed.
- **The member.** Top-level Args gain `std::optional<std::string_view> message_encoding;`, appended
  **last**.
- **The emit.** When set, the builder emits `bb.field(347, *args.message_encoding)` as the **first**
  body field.
- **Messages without an `Encoded*` field** get no member. A test asserts one such message per version
  (e.g. v44 `Heartbeat` or whichever app message the census names) has no `message_encoding` member,
  via a `requires` expression.
- **Accessor collision** is resolved by the existing `uniquify_accessor`.

## C-2.4 Goldens (FR-013)

- **078 builder goldens** are regenerated for v42/v44/v50sp2/vlatest. The diff is restricted to
  C-2.1's rewrite and C-2.3's member and emit. The review recipe:
  `git diff --stat` per version, plus `git diff -U0 | grep '^[+-]' | grep -vE 'field_data|set_data|static_assert|r_pair|r_len|r_data|message_encoding|347'`,
  which must print only diff headers.
- **Read-tier SHA-256 pins** (`tests/codegen/read_tier_byte_diff_test.cmake`): unchanged. If any
  moves, stop and report; it means the change leaked into the read tier.
