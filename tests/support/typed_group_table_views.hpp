// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/typed_group_table_views.hpp
//
// 220 — shared membership for the typed-group test cells.
//
// WHY THIS EXISTS. fixpp#220 made repeating-group identification a
// dictionary-only operation: `OffsetTable::group()` declines when no
// membership predicate is threaded, so a dict-FREE parse yields no typed group
// at all. Several codegen cells parsed dict-free and asserted a NON-EMPTY
// `NewOrderList::orders()`; they need a dictionary, and they need the SAME one,
// which is why this is a header rather than a copy per TU.
//
// WHY IT IS HAND-BUILT RATHER THAN THE SHIPPED FIX44.xml. Those cells drive
// SYNTHETIC frames — `35=E` carrying group 73 with members 11/37/38/54 — which
// do not match FIX44's real ListOrdGrp member set. Loading the shipped
// dictionary splits such a frame into one instance instead of two (measured),
// so the cells would be testing the dictionary rather than the generated entry
// accessors they exist for. The member set below is exactly the tags those
// frames use.
//
// LIFETIME. A `MessageView` stores the table_view by ADDRESS (`opaque_dict_`),
// so this must outlive every view built over it — hence the function-local
// static rather than a value returned by copy.
#pragma once

#include <fixpp/dict/table_view.hpp>

namespace fixpp_test_support {

inline fixpp::dict::table_view& group73_table_view() {
    static fixpp::dict::table_view tv = [] {
        fixpp::dict::table_view t;
        t.add_valid("E", 35)
            .add_valid("E", 73)
            .add_valid("E", 11)
            .add_valid("E", 37)
            .add_valid("E", 38)
            .add_valid("E", 54)
            .set_group_first(73, 11)
            .add_group_member(73, 37)
            .add_group_member(73, 38)
            .add_group_member(73, 54);
        return t;
    }();
    return tv;
}

// The `35=AB` frames in tests/wire/repeating_group_equivalence_test.cpp: two
// TOP-LEVEL groups in one message — 555 (NoLegs, delimiter 600, member 608)
// and 604 (delimiter 605). 604 is a nested group in real FIX; these frames use
// it at top level, which is another reason the shipped dictionary is the wrong
// oracle for them.
inline fixpp::dict::table_view& legs_and_alt_table_view() {
    static fixpp::dict::table_view tv = [] {
        fixpp::dict::table_view t;
        t.add_valid("AB", 35)
            .add_valid("AB", 34)
            .add_valid("AB", 555)
            .add_valid("AB", 600)
            .add_valid("AB", 608)
            .add_valid("AB", 604)
            .add_valid("AB", 605)
            .set_group_first(555, 600)
            .add_group_member(555, 608)
            .set_group_first(604, 605);
        return t;
    }();
    return tv;
}

}  // namespace fixpp_test_support
