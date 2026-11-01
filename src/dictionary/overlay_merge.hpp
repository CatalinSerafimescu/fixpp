// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/overlay_merge.hpp
//
// 098-dialect-overlay, internal to `src/dictionary/` (and the dictionary tests,
// whose include path already has this directory). The QuickFIX-XML half of the
// overlay load: research R-5's guard stage (steps 1 to 7), R-3's splice (step 8)
// and the Length+Data relation check (step 8a). `xml_loader.cpp` owns the build
// (step 9), the step counter and R-5's read seam declared at the end of this file.
//
// Order of the guard stage (research R-5): byte cap, parse, DOCTYPE, depth, root,
// entry and reference caps, the merged-graph walk (cycles, expansion budget,
// side-table fit), conflicts. Every step is iterative. No step before the walk
// follows a reference, and the walk runs before the conflict checks, the splice
// and the build (FR-010).

#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/loader_policy.hpp>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp::dict::detail::overlay {

// ── Caps (research R-5 step 1, 4 and 5). Fixed; no configuration knob. Each
// admits its value and refuses its value + 1.
inline constexpr std::size_t kMaxOverlayBytes = std::size_t{4} * 1024U * 1024U;  // 2c §4.4.2
inline constexpr std::size_t kMaxDepth = 32;            // 2c §4.4.2; the root is level 1
inline constexpr std::size_t kMaxNewFields = 1024;      // 2c §4.4.2
inline constexpr std::size_t kMaxNewComponents = 64;    // 2c §4.4.2
inline constexpr std::size_t kMaxNewMessages = 64;      // 2c §4.4.2
inline constexpr std::size_t kMaxNewEnumValues = 4096;  // research R-5, a chosen value
inline constexpr std::size_t kMaxReferences = 8192;     // research R-5, a chosen value

// The expansion budget (research R-5, "The expansion budget"): run step 6's count
// over every vendored base with an empty overlay, take the largest total, and set
// the budget to the next power of two at or above twice it. W-3's budget recipe
// row recomputes that recipe through the read seam and asserts this constant
// equals its result.
inline constexpr std::uint64_t kExpansionBudget = std::uint64_t{1} << 20U;

// The most a side-table start or count may be and still fit the `std::uint16_t`
// that `finalize` stores it in (research R-5 step 6, side-table fit).
inline constexpr std::uint64_t kSideTableMax = 65535;

// Step 2: the overlay's parse flags. The base is parsed with `load`'s flags.
inline constexpr unsigned kOverlayParseFlags = pugi::parse_default | pugi::parse_doctype;

enum class source_format : std::uint8_t { quickfix_xml, orchestra };

// Step 2's status mapping, shared by both parses of one overlay call: a failed
// parse throws `xml_oom_error` for `status_out_of_memory` and the format's parse
// error (`xml_parse_error`, or `orchestra_parse_error`) for any other failure.
// Returns normally when the parse succeeded.
void throw_on_parse_failure(pugi::xml_parse_result const& result, source_format format);

// Step 1, the string form: refuses a size above the byte cap.
void check_overlay_size(std::size_t size);

// Step 1, the path form: reads at most cap + 1 bytes and refuses when the last of
// them exists. Never asks the file system for a size. A path that cannot be opened
// or read throws the format's parse error.
[[nodiscard]] std::string read_overlay_file(std::filesystem::path const& path,
                                            source_format format);

// Steps 3 and 4 (format-independent).
void check_no_doctype(pugi::xml_document const& overlay);
void check_depth(pugi::xml_document const& overlay);

// Which of the guard stage's limit checks, or the build's counter, refused a load.
enum class refusal_origin : std::uint8_t { none, budget, side_table, counter };

// The guard stage's step totals and side-table fits. `arm_limit` is the value the
// build's step counter is armed with: the smaller of the merged total and the base's total plus the
// budget (research R-5, the step counter).
struct guard_outcome {
    std::uint64_t base_total = 0;
    std::uint64_t merged_total = 0;
    std::uint64_t arm_limit = 0;
    bool base_components_fit = true;
    bool base_groups_fit = true;
    bool merged_components_fit = true;
    bool merged_groups_fit = true;
};

// Written by the overlay load as it goes, so the read seam can report where a
// refusal came from. A local of each call.
struct overlay_trace {
    refusal_origin origin = refusal_origin::none;
    std::optional<guard_outcome> guards;
    std::uint64_t counter = 0;  // the build's step counter, once the build ran
};

// The Length+Data pair relation of a QuickFIX-XML document (research R-3):
// `forward` is `LoaderState::detect_length_pairs`'s Length → Data map with its
// first-writer rule; `reverse` is its inverse with every Length kept; and
// `declared_tags` is every field number the document declares, sorted.
struct length_pair_relation {
    std::map<std::uint16_t, std::uint16_t> forward;
    std::map<std::uint16_t, std::vector<std::uint16_t>> reverse;
    std::vector<std::uint16_t> declared_tags;
};

// Defined in xml_loader.cpp: runs the loader's own version and `<fields>` parse
// on `doc` and then the pair detection `LoaderState` itself uses, so this and the
// loader's pairing cannot drift. Throws whatever those loader steps throw.
[[nodiscard]] length_pair_relation xml_length_pair_relation(pugi::xml_document const& doc);

// Step 8a: refuses with `overlay_conflict_error` when a pair touching a field
// number the base declares is gained, lost or given a second partner.
void check_pair_relation(length_pair_relation const& base, length_pair_relation const& spliced);

// The QuickFIX-XML merge of one overlay into one base document: the classifier
// that steps 5, 6, 7 and 8 share, the guard routine and the splice.
class xml_overlay_merge {
public:
    // Both documents must outlive this object. `base` is changed only by `splice`.
    xml_overlay_merge(pugi::xml_document& base, pugi::xml_document const& overlay);
    ~xml_overlay_merge();
    xml_overlay_merge(xml_overlay_merge const&) = delete;
    xml_overlay_merge& operator=(xml_overlay_merge const&) = delete;
    xml_overlay_merge(xml_overlay_merge&&) = delete;
    xml_overlay_merge& operator=(xml_overlay_merge&&) = delete;

    // The guard routine each overlay entry point calls before the splice: research
    // R-5's steps from the DOCTYPE check through the conflict checks, in R-5's order.
    // Writes the step totals into `trace`, and the origin before a budget or
    // side-table refusal. Returns the step counter's
    // arm value.
    [[nodiscard]] std::uint64_t guard(unresolved_group_policy policy, overlay_trace& trace);

    // Step 8 (research R-3). Call only after `guard` returned.
    void splice();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// ── R-5's read seam (defined in xml_loader.cpp; tests only). Calls
// `XmlLoader::load_with_overlay[_from_string]` itself and also returns the
// counter's final value, step 6's predicted total and the origin of a refusal,
// which those members report through a thread-local channel only the seam sets.
// A refusal is returned in `error`, never thrown.
struct xml_overlay_probe {
    std::optional<Dictionary> dictionary;
    std::exception_ptr error;
    refusal_origin origin = refusal_origin::none;
    std::optional<guard_outcome> guards;  // set once step 6 has run
    std::uint64_t counter = 0;
};

[[nodiscard]] xml_overlay_probe probe_xml_overlay_from_string(
    std::string_view base_xml, std::string_view overlay_xml, std::pmr::memory_resource* mr,
    unresolved_group_policy policy = unresolved_group_policy::fail_closed);

[[nodiscard]] xml_overlay_probe probe_xml_overlay(
    std::filesystem::path const& base, std::filesystem::path const& overlay,
    std::pmr::memory_resource* mr,
    unresolved_group_policy policy = unresolved_group_policy::fail_closed);

}  // namespace fixpp::dict::detail::overlay
