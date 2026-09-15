// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/length_data_pairs.hpp — the standard FIX Length+Data pairs.
//
// A Data field may carry SOH; the Length field that immediately precedes it gives
// the value's byte count (FIX TagValue Encoding v1.0 §4.2.5, §4.3.7.3). Every
// dictionary-free field scanner uses this table to find that pair (fixpp#426).
//
// The rows are the union of every pair the ten shipped dictionaries declare, which
// equals the pairs FIX Latest's Orchestra repository names with `lengthId=`. The
// wire test `LengthDataPairs.HeaderEqualsShippedDictionaryUnion` loads each
// dictionary and fails, printing the replacement rows, whenever they diverge.
// Do not edit rows by hand.
//
// A leaf header on purpose: offset_table.cpp and parser.hpp both include it.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace fixpp::wire::detail {

struct length_data_pair {
    std::uint16_t length_tag;
    std::uint16_t data_tag;
};

// Sorted by length_tag (asserted below). Some Data tags precede their Length tag
// numerically (89/93, 2371/2372, …) and some are not adjacent (1527/1525, 1697/1678).
inline constexpr std::array<length_data_pair, 84> standard_length_data_pairs{{
    {.length_tag = 90, .data_tag = 91},      // SecureDataLen / SecureData
    {.length_tag = 93, .data_tag = 89},      // SignatureLength / Signature
    {.length_tag = 95, .data_tag = 96},      // RawDataLength / RawData
    {.length_tag = 212, .data_tag = 213},    // XmlDataLen / XmlData
    {.length_tag = 348, .data_tag = 349},    // EncodedIssuerLen / EncodedIssuer
    {.length_tag = 350, .data_tag = 351},    // EncodedSecurityDescLen / EncodedSecurityDesc
    {.length_tag = 352, .data_tag = 353},    // EncodedListExecInstLen / EncodedListExecInst
    {.length_tag = 354, .data_tag = 355},    // EncodedTextLen / EncodedText
    {.length_tag = 356, .data_tag = 357},    // EncodedSubjectLen / EncodedSubject
    {.length_tag = 358, .data_tag = 359},    // EncodedHeadlineLen / EncodedHeadline
    {.length_tag = 360, .data_tag = 361},    // EncodedAllocTextLen / EncodedAllocText
    {.length_tag = 362, .data_tag = 363},    // EncodedUnderlyingIssuerLen / …
    {.length_tag = 364, .data_tag = 365},    // EncodedUnderlyingSecurityDescLen / …
    {.length_tag = 445, .data_tag = 446},    // EncodedListStatusTextLen / …
    {.length_tag = 618, .data_tag = 619},    // EncodedLegIssuerLen / EncodedLegIssuer
    {.length_tag = 621, .data_tag = 622},    // EncodedLegSecurityDescLen / …
    {.length_tag = 1184, .data_tag = 1185},  // SecurityXMLLen / SecurityXML
    {.length_tag = 1277, .data_tag = 1278},  // DerivativeEncodedIssuerLen / …
    {.length_tag = 1280, .data_tag = 1281},  // DerivativeEncodedSecurityDescLen / …
    {.length_tag = 1282, .data_tag = 1283},  // DerivativeSecurityXMLLen / …
    {.length_tag = 1397, .data_tag = 1398},  // EncodedMktSegmDescLen / …
    {.length_tag = 1401, .data_tag = 1402},  // EncryptedPasswordLen / EncryptedPassword
    {.length_tag = 1403, .data_tag = 1404},  // EncryptedNewPasswordLen / …
    {.length_tag = 1468, .data_tag = 1469},  // EncodedSecurityListDescLen / …
    {.length_tag = 1525, .data_tag = 1527},  // EncodedDocumentationTextLen / …
    {.length_tag = 1578, .data_tag = 1579},  // EncodedEventTextLen / EncodedEventText
    {.length_tag = 1620, .data_tag = 1621},  // InstrumentScopeEncodedSecurityDescLen / …
    {.length_tag = 1664, .data_tag = 1665},  // EncodedRejectTextLen / EncodedRejectText
    {.length_tag = 1678, .data_tag = 1697},  // EncodedOptionExpirationDescLen / …
    {.length_tag = 1733, .data_tag = 1734},  // EncodedFirmAllocTextLen / …
    {.length_tag = 1871, .data_tag = 1872},  // LegSecurityXMLLen / LegSecurityXML
    {.length_tag = 1874, .data_tag = 1875},  // UnderlyingSecurityXMLLen / …
    {.length_tag = 2072, .data_tag = 2073},  // EncodedUnderlyingEventTextLen / …
    {.length_tag = 2074, .data_tag = 2075},  // EncodedLegEventTextLen / …
    {.length_tag = 2111, .data_tag = 2112},  // EncodedAttachmentLen / EncodedAttachment
    {.length_tag = 2179, .data_tag = 2180},  // EncodedLegOptionExpirationDescLen / …
    {.length_tag = 2287, .data_tag = 2288},  // EncodedUnderlyingOptionExpirationDescLen / …
    {.length_tag = 2351, .data_tag = 2352},  // EncodedComplianceTextLen / …
    {.length_tag = 2372, .data_tag = 2371},  // EncodedTradeContinuationTextLen / …
    {.length_tag = 2481, .data_tag = 2482},  // EncodedMDStatisticDescLen / …
    {.length_tag = 2494, .data_tag = 2493},  // EncodedLegDocumentationTextLen / …
    {.length_tag = 2522, .data_tag = 2521},  // EncodedWarningTextLen / EncodedWarningText
    {.length_tag = 2637, .data_tag = 2638},  // EncodedMiscFeeSubTypeDescLen / …
    {.length_tag = 2651, .data_tag = 2652},  // EncodedCommissionDescLen / …
    {.length_tag = 2665, .data_tag = 2666},  // EncodedAllocCommissionDescLen / …
    {.length_tag = 2715, .data_tag = 2716},  // EncodedFinancialInstrumentFullNameLen / …
    {.length_tag = 2718, .data_tag = 2719},  // EncodedLegFinancialInstrumentFullNameLen / …
    {.length_tag = 2721, .data_tag = 2722},  // EncodedUnderlyingFinancialInstrumentFullNameLen / …
    {.length_tag = 2797, .data_tag = 2798},  // EncodedMatchExceptionTextLen / …
    {.length_tag = 2802, .data_tag = 2801},  // EncodedReplaceTextLen / EncodedReplaceText
    {.length_tag = 2809, .data_tag = 2808},  // EncodedCancelTextLen / EncodedCancelText
    {.length_tag = 2815, .data_tag = 2814},  // EncodedPostTradePaymentDescLen / …
    {.length_tag = 2971, .data_tag = 2972},  // EncodedSettlStatusReasonTextLen / …
    {.length_tag = 40004, .data_tag = 40005},  // EncodedAdditionalTermBondDescLen / …
    {.length_tag = 40008, .data_tag = 40009},  // EncodedAdditionalTermBondIssuerLen / …
    {.length_tag = 40978, .data_tag = 40979},  // EncodedLegStreamTextLen / …
    {.length_tag = 40980, .data_tag = 40981},  // EncodedLegProvisionTextLen / …
    {.length_tag = 40982, .data_tag = 40983},  // EncodedStreamTextLen / EncodedStreamText
    {.length_tag = 40984, .data_tag = 40985},  // EncodedPaymentTextLen / EncodedPaymentText
    {.length_tag = 40986, .data_tag = 40987},  // EncodedProvisionTextLen / …
    {.length_tag = 40988, .data_tag = 40989},  // EncodedUnderlyingStreamTextLen / …
    {.length_tag = 41083, .data_tag = 41084},  // EncodedDeliveryStreamCycleDescLen / …
    {.length_tag = 41101,
     .data_tag = 41102},  // EncodedMarketDisruptionFallbackUnderlierSecurityDescLen / …
    {.length_tag = 41107, .data_tag = 41108},  // EncodedExerciseDescLen / …
    {.length_tag = 41256, .data_tag = 41257},  // EncodedStreamCommodityDescLen / …
    {.length_tag = 41320, .data_tag = 41321},  // EncodedLegAdditionalTermBondDescLen / …
    {.length_tag = 41324, .data_tag = 41325},  // EncodedLegAdditionalTermBondIssuerLen / …
    {.length_tag = 41458, .data_tag = 41459},  // EncodedLegDeliveryStreamCycleDescLen / …
    {.length_tag = 41476,
     .data_tag = 41477},  // EncodedLegMarketDisruptionFallbackUnderlierSecurityDescLen / …
    {.length_tag = 41482, .data_tag = 41483},  // EncodedLegExerciseDescLen / …
    {.length_tag = 41653, .data_tag = 41654},  // EncodedLegStreamCommodityDescLen / …
    {.length_tag = 41710, .data_tag = 41711},  // EncodedUnderlyingAdditionalTermBondDescLen / …
    {.length_tag = 41806, .data_tag = 41807},  // EncodedUnderlyingDeliveryStreamCycleDescLen / …
    {.length_tag = 41811, .data_tag = 41812},  // EncodedUnderlyingExerciseDescLen / …
    {.length_tag = 41873,
     .data_tag = 41874},  // EncodedUnderlyingMarketDisruptionFallbackUnderlierSecDescLen / …
    {.length_tag = 41969, .data_tag = 41970},  // EncodedUnderlyingStreamCommodityDescLen / …
    {.length_tag = 42025, .data_tag = 42026},  // EncodedUnderlyingAdditionalTermBondIssuerLen / …
    {.length_tag = 42171, .data_tag = 42172},  // EncodedUnderlyingProvisionTextLen / …
    {.length_tag = 42451, .data_tag = 42452},  // LegPaymentStreamFormulaImageLength / …
    {.length_tag = 42652, .data_tag = 42653},  // PaymentStreamFormulaImageLength / …
    {.length_tag = 42947, .data_tag = 42948},  // UnderlyingPaymentStreamFormulaImageLength / …
    {.length_tag = 43109, .data_tag = 42684},  // PaymentStreamFormulaLength / PaymentStreamFormula
    {.length_tag = 43110, .data_tag = 42486},  // LegPaymentStreamFormulaLength / …
    {.length_tag = 43111, .data_tag = 42982},  // UnderlyingPaymentStreamFormulaLength / …
}};

// The binary search below needs strictly ascending Length tags.
inline constexpr bool standard_length_data_pairs_sorted =
    std::ranges::is_sorted(standard_length_data_pairs, std::ranges::less{},
                           &length_data_pair::length_tag) &&
    std::ranges::adjacent_find(standard_length_data_pairs, {}, &length_data_pair::length_tag) ==
        standard_length_data_pairs.end();
static_assert(standard_length_data_pairs_sorted);

// The Data tag paired with `length_tag` by the FIX standard, or 0.
[[nodiscard]] constexpr std::uint16_t standard_data_tag_for_length(
    std::uint16_t length_tag) noexcept {
    auto const it = std::ranges::lower_bound(standard_length_data_pairs, length_tag,
                                             std::ranges::less{}, &length_data_pair::length_tag);
    return (it != standard_length_data_pairs.end() && it->length_tag == length_tag) ? it->data_tag
                                                                                    : 0;
}

// The same pairs sorted by Data tag, for the Data->Length lookup. Derived from the
// table above rather than written a second time.
inline constexpr std::array<length_data_pair, standard_length_data_pairs.size()>
    standard_pairs_by_data = [] {
        auto out = standard_length_data_pairs;
        std::ranges::sort(out, std::ranges::less{}, &length_data_pair::data_tag);
        return out;
    }();
// No Data tag is paired twice, so the inverse is unambiguous.
static_assert(std::ranges::adjacent_find(standard_pairs_by_data, {}, &length_data_pair::data_tag) ==
              standard_pairs_by_data.end());

// The Length tag paired with `data_tag` by the FIX standard, or 0.
[[nodiscard]] constexpr std::uint16_t standard_length_tag_for_data(
    std::uint16_t data_tag) noexcept {
    auto const it = std::ranges::lower_bound(standard_pairs_by_data, data_tag, std::ranges::less{},
                                             &length_data_pair::data_tag);
    return (it != standard_pairs_by_data.end() && it->data_tag == data_tag) ? it->length_tag : 0;
}

// The two directions agree on every standard pair.
static_assert(std::ranges::all_of(standard_length_data_pairs, [](length_data_pair const p) {
    return standard_data_tag_for_length(p.length_tag) == p.data_tag &&
           standard_length_tag_for_data(p.data_tag) == p.length_tag;
}));
static_assert(standard_length_tag_for_data(89) == 93);  // inverted pair
static_assert(standard_length_tag_for_data(93) == 0);   // a Length tag is not a Data tag

// True iff `tag` is the Length or the Data half of a STANDARD pair. Used to
// exclude a dictionary's own Length+Data pairs from re-pairing or retyping a
// standard tag (dict_hooks::data_tag_for_length, design §3, r3 R3-1). Answered
// from the table and its inverse above, not from a third copy of the same fact.
[[nodiscard]] constexpr bool is_standard_pair_tag(std::uint16_t tag) noexcept {
    return standard_data_tag_for_length(tag) != 0 || standard_length_tag_for_data(tag) != 0;
}

static_assert(is_standard_pair_tag(93) && is_standard_pair_tag(89));    // inverted pair, both sides
static_assert(is_standard_pair_tag(354) && is_standard_pair_tag(355));  // adjacent pair, both sides
static_assert(!is_standard_pair_tag(5001));                             // an arbitrary non-pair tag

// One bit per 16-bit tag, set for both halves of every standard pair: the same
// set `is_standard_pair_tag` answers, derived from the table above. Every scanner
// asks about every field and almost no field is a pair tag, so the hot path
// (dict_hooks::data_tag_for_length) tests one bit before any binary search.
inline constexpr std::array<std::uint64_t, 1024> standard_pair_tag_bits = [] {
    std::array<std::uint64_t, 1024> bits{};
    for (length_data_pair const p : standard_length_data_pairs) {
        bits[p.length_tag >> 6U] |= std::uint64_t{1} << (p.length_tag & 63U);
        bits[p.data_tag >> 6U] |= std::uint64_t{1} << (p.data_tag & 63U);
    }
    return bits;
}();
// Every table tag is set and nothing else is: the table sets at most one bit per
// entry half, so a total equal to the entry halves means no two halves share a bit.
static_assert([] {
    int set = 0;
    for (std::uint64_t const word : standard_pair_tag_bits) {
        set += std::popcount(word);
    }
    return set;
}() == static_cast<int>(2 * standard_length_data_pairs.size()));

[[nodiscard]] constexpr bool standard_pair_tag_bit(std::uint16_t tag) noexcept {
    return ((standard_pair_tag_bits[tag >> 6U] >> (tag & 63U)) & 1U) != 0;
}

static_assert(standard_pair_tag_bit(93) && standard_pair_tag_bit(89));
static_assert(standard_pair_tag_bit(43111) && !standard_pair_tag_bit(5001));

}  // namespace fixpp::wire::detail
