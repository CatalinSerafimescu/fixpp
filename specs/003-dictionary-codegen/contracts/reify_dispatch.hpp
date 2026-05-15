// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 1 contract — the SHAPE of the two GENERATED shared dispatch headers
// ([2c §4.8] / [2c §6.3]; research.md D-6). NOT hand-written. Emitted into
// build/<preset>/_codegen/include/fixpp/_dispatch/ once (dispatch-shared, not
// per-version). Owned by the fixpp::dict::dispatch CMake target which depends
// on fixpp::dict::all_versions ([2c §7.6]). Included once per dispatch-
// consuming TU, never four times.
#pragma once

// ── _dispatch/reify_dispatch_fixt.hpp ───────────────────────────────────────
// Exactly the 7 FIXT admin MsgTypes. On hit → vt11::owning_<Msg> wrapped in
// owning_message_handle with {session_admin, profile.session, Unknown}.
// AC-D1 / AC-D2 / seam #15a.
//
//   switch (msg_type) {
//     case '0': /* Heartbeat     */ return make<fixpp::vt11::owning_Heartbeat>(...);
//     case '1': /* TestRequest   */ return make<fixpp::vt11::owning_TestRequest>(...);
//     case '2': /* ResendRequest */ return make<fixpp::vt11::owning_ResendRequest>(...);
//     case '3': /* Reject        */ return make<fixpp::vt11::owning_Reject>(...);
//     case '4': /* SequenceReset */ return make<fixpp::vt11::owning_SequenceReset>(...);
//     case '5': /* Logout        */ return make<fixpp::vt11::owning_Logout>(...);
//     case 'A': /* Logon         */ return make<fixpp::vt11::owning_Logon>(...);
//     default : return std::unexpected{error::dict_reify_unknown_msg_type};
//   }

// ── _dispatch/reify_dispatch_application.hpp ────────────────────────────────
// One case per (codegen application_version, MsgType) across v42/v44/v50sp2
// (~470 cases). AC-D1 / AC-D3 / AC-D7 / seam #15b.
//
//   switch (resolved_application) {
//     case application_version::v50sp2:
//       switch (msg_type) {
//         case "D":  return make<fixpp::v50sp2::owning_NewOrderSingle>(...);
//         case "8":  return make<fixpp::v50sp2::owning_ExecutionReport>(...);
//         /* ... every emitted MsgType for v50sp2 ... */
//         default :  return std::unexpected{error::dict_reify_unknown_msg_type};
//       }
//     case application_version::v44:    /* ... */
//     case application_version::v42:    /* ... */
//     // Runtime-XML-only resolved versions (v40/v41/v43/v50/v50sp1) have NO
//     // codegen owner → fall to the fail-loud default (AC-D5 / seam #10c).
//     default: return std::unexpected{error::dict_reify_unknown_msg_type};
//   }
//
// INVARIANT I-11 (data-model): the default arm is fail-loud — it returns the
// documented error, it never misdispatches silently (R3 mitigation).
// Exhaustiveness is the conformance corpus's job: CI runs the AC-G12 curated
// must-include subset; the nightly run is exhaustive over ~470 entries.
