// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/sent_record_intent.hpp — 089 T048.
//
// The seam C-8 (contracts/readback-jsonl.md) requires and no production call
// site provided yet: "a sent record whose `fields` are derived from the
// serialized frame is a violation... the sent record MUST be derived from
// the builder inputs and never re-read from the serialized frame" (FR-006).
// tasks.md T048 requires this proven against "the production function that
// builds fixpp's sent record, the one T052's cells will call, not a
// test-local re-implementation" — that function did not exist before this
// file (T052, which would otherwise have introduced it, has not landed).
// Built on fixpp::wire::body_builder, the shipped 061 body-only serializer
// every typed builder (build_new_order_single, build_execution_report,
// build_new_order_list) already goes through — not a test-local
// reimplementation of message construction.
//
// Message: OrderCancelRequest (35=F), census step B-03 (spec.md §
// "Conversation census"). ClOrdID(11), OrigClOrdID(41), Account(1) — the
// exact field quickstart.md's C-8 arm names (path "1").
#pragma once

#include "readback_jsonl.hpp"

#include <fixpp/core/error.hpp>
#include <fixpp/wire/body_builder.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp::interop::readback {

// THE PRODUCTION FUNCTION (C-8): fixpp's `sent.fields` for an
// OrderCancelRequest, built from BUILDER INPUTS — the same three strings a
// T052 cell would read from the conversation script (FR-008a) and hand to
// build_order_cancel_request() below — captured BEFORE that call, never
// re-read from its output. This is what a real cell calls for this step's
// `sent` record.
inline std::vector<FieldEntry> order_cancel_request_sent_fields_from_intent(
    std::string_view cl_ord_id, std::string_view orig_cl_ord_id, std::string_view account)
{
    return {
        {"11", std::string(cl_ord_id)},
        {"41", std::string(orig_cl_ord_id)},
        {"1", std::string(account)},
    };
}

// The production write path for the SAME intent — fixpp::wire::body_builder,
// not a test-local byte-pusher. Emits "35=F\x01" (MsgType, header — C-6)
// then the three fields in author order.
[[nodiscard]] inline fixpp::core::expected_t<std::span<std::byte>> build_order_cancel_request(
    std::span<std::byte> out, std::string_view cl_ord_id, std::string_view orig_cl_ord_id,
    std::string_view account) noexcept
{
    fixpp::wire::body_builder bb("F");
    if (auto r = bb.field(std::uint16_t{11}, cl_ord_id); !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (auto r = bb.field(std::uint16_t{41}, orig_cl_ord_id); !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (auto r = bb.field(std::uint16_t{1}, account); !r.has_value()) {
        return std::unexpected(r.error());
    }
    return bb.commit(out);
}

}  // namespace fixpp::interop::readback
