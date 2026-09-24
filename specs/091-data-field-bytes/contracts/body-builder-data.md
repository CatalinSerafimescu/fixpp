# Contract C-1 — `body_builder` / `entry_handle` Data-field operation

Public C++ API, `include/fixpp/wire/body_builder.hpp`. The design rationale is in research.md R-1 to
R-4. Refusal codes are in data-model.md.

## Declarations (the shape to implement)

```cpp
namespace fixpp::wire {

class entry_handle {
public:
    // Appends <Length>=<octet count> then <data_tag>=<value>, both or neither.
    // The Length tag is the one the owning builder's pair lookup pairs with data_tag.
    // `value` is copied verbatim; any octet 0x00-0xFF is accepted.
    [[nodiscard]] fixpp::core::expected_t<void> set_data(
        std::uint16_t data_tag, std::span<const std::byte> value) noexcept;
    // ...existing members unchanged...
};

class body_builder {
public:
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

## Behaviour clauses (each clause maps to at least one test)

- **C-1.1 success.** `field_data(355, {0x41,0x01,0x42})` then `commit` yields a body containing
  `354=3<SOH>355=A<SOH>B<SOH>`, in that order, at the position of the call.
- **C-1.2 every octet.** For every `b` in `0x00–0xFF`, `field_data(355, {b})` commits, and a re-parse
  through the inbound parser recovers exactly `{b}` as tag 355. The same holds through `set_data` in
  a group entry (SC-001).
- **C-1.3 multi-digit Length.** A value of N octets (N ≥ 10, and a value near the body cap) emits
  `354=<N>` with no leading zeros.
- **C-1.4 refusals.** These are refused, with nothing appended (the container size is unchanged):
  - a non-Data tag (`11`, `354`, i.e. a Length half, `8`);
  - an empty value;
  - a framing Length from a dictionary pair.
- **C-1.5 rollback.**
  - On arena exhaustion, `field_data` returns `wire_frame_too_large` and neither node remains.
  - A subsequent in-budget `field()` still works.
  - An over-cap total fails at commit with `out` untouched.
- **C-1.6 INV-2 unchanged.** `SohInValue_RejectedBeforeAnyByteReachesOut` passes **byte-identical**
  to `origin/main`. `field(355, "A\x01B")` (the string path on a Data tag) is still refused by the
  content guard.
- **C-1.7 commit pair check (INV-6).** Each case commits with `wire_invalid_field_format` and `out`
  untouched:
  - `field(354, int 3)` with no following 355;
  - `field(355, "abc")` with no preceding 354;
  - `field(354, int 4)` + `field(355, "abc")`;
  - a group node between 354 and 355;
  - `field(354, "0")` + `field(355, "x")`.

  A hand-written, well-formed `field(354, int 3)` + `field(355, "abc")` commits.
- **C-1.8 per-container.** The check runs separately in the top level and in each group instance. A
  Length at the end of one instance does not pair with a Data at the start of the next.
- **C-1.9 dictionary pairs (FR-009).** A builder constructed with a `dict_hooks` whose dictionary
  declares a pair (5001, 5002) accepts `field_data(5002, ...)`, emitting `5001=` first. A default
  builder refuses it (`wire_unexpected_tag`). A dictionary re-pairing a standard tag (95 → 5002) is
  ignored in favour of the standard pair (L-426-2), the same way `dict_hooks_custom_pair_test`
  exercises the inbound side.
- **C-1.10 Length-delimited entry.** An entry of a group whose `delimiter_tag` is 43109, populated by
  `set_data(42684, ...)`, commits (INV-5 holds because the Length node is first).
- **C-1.11 duplicate pair.** Calling `field_data(355, ...)` twice appends two pairs, the same
  append-not-upsert semantics `field()` has today. Both are well-formed, so commit accepts. This is
  not a new policy; it is recorded so a reviewer does not read it as a gap.

## Non-goals

- No `std::string_view` overload (R-1).
- `body_builder` does not know FIX types. A caller may still `field_data` a Data tag the message does
  not declare; per-message membership stays the job of the generated builders and the validator.
