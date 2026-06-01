// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/scenario_descriptor.hpp
//
// Descriptive interop test-fixture types for specs/016-interop-harness. These
// are not a frozen public API and carry no dependency on production fixpp
// types. Every executed cell MUST carry a non-empty spec_ref (FR-018); a cell
// justified only by reference-engine behavior is invalid.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fixpp::interop {

enum class ScenarioKind : std::uint8_t { happy, thorny, parity };
enum class Counterparty : std::uint8_t { quickfix_cpp, quickfix_j, fix8 };
enum class Role : std::uint8_t { fixpp_initiator, fixpp_acceptor };
enum class Security : std::uint8_t { plain_tcp, tls_logon, mtls_mutual };
enum class RunState : std::uint8_t { pending, running, passed, failed, skipped, known_limitation };

enum class CellDisposition : std::uint8_t {
    live,
    deferred_app_messages,
    deferred_fixt_routing,
    deferred_fix8_revisit,
    deferred_v11_mtls
};

enum class ParityDisposition : std::uint8_t { covered, gap, not_applicable };

enum class CorpusPriority : std::uint8_t {
    p1,
    p2,
    p3,
    watch_p1,
    watch_p2,
    watch_info
};

enum class ProvenanceState : std::uint8_t { closed, open };

struct SeqnumDelta {
    int in = 0;
    int out = 0;
};

struct PassCriteria {
    std::string fixpp_end_state;
    std::string counterparty_terminal;
    SeqnumDelta seqnum_delta;
    bool golden_match = false;
};

struct Scenario {
    std::string id;
    ScenarioKind kind = ScenarioKind::happy;
    std::string preconditions;
    std::string driven_sequence;
    PassCriteria pass_criteria;
    std::chrono::milliseconds deadline{0};
    std::optional<std::string> skip_reason;
    std::string spec_ref;
    RunState state = RunState::pending;
};

struct MatrixCell : Scenario {
    Counterparty counterparty = Counterparty::quickfix_cpp;
    Role role = Role::fixpp_initiator;
    std::string fix_version = "FIX.4.4";
    Security security = Security::plain_tcp;
    std::string event_chain;
    std::string business = "none";
    std::string golden_ref;
    CellDisposition disposition = CellDisposition::live;
    std::optional<int> reconnect_max_attempts;
    std::optional<std::chrono::milliseconds> stop_watchdog;
};

struct Provenance {
    std::string engine;
    std::string ref;
    std::string url;
    ProvenanceState pstate = ProvenanceState::closed;
};

struct CorpusScenario : Scenario {
    Provenance provenance;
    std::string category;
    CorpusPriority priority = CorpusPriority::p1;
    bool differentiator = false;
    std::optional<std::string> known_limitation;
};

struct ParityRow {
    std::string source;
    std::string behavior;
    ParityDisposition disposition = ParityDisposition::gap;
    std::string citation;
};

inline bool has_spec_ref(const Scenario& s)
{
    return !s.spec_ref.empty();
}

}  // namespace fixpp::interop
