// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/length_data_pairs.hpp — the standard pair table, under the
// wire-layer spelling its callers use.
//
// The table itself is <fixpp/core/length_data_pairs.hpp>: the dictionary layer needs
// it as well (table_view tells a dictionary's own pairs from the standard ones), and
// dictionary may not include wire ([arch §2.3]). Keeping the names here means no
// wire caller changed when it moved.

#pragma once

#include <fixpp/core/length_data_pairs.hpp>

namespace fixpp::wire::detail {

using core::detail::is_standard_pair_tag;
using core::detail::length_data_pair;
using core::detail::standard_data_tag_for_length;
using core::detail::standard_length_data_pairs;
using core::detail::standard_length_data_pairs_sorted;
using core::detail::standard_length_tag_for_data;
using core::detail::standard_pair_tag_bit;
using core::detail::standard_pair_tag_bits;
using core::detail::standard_pairs_by_data;

}  // namespace fixpp::wire::detail
