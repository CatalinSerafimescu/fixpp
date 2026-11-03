// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/fuzz/fuzz_dict_overlay.cpp
//
// 098-dialect-overlay, contracts/witnesses.md W-11 (research R-11): libFuzzer
// entry point for the dialect overlay loader, per `[const §VII.7]` (new
// parser-touching code over untrusted XML needs a fuzz harness).
//
// Input: the first byte chooses the format (even: QuickFIX XML; odd: Orchestra)
// and the rest of the input is the overlay. The base is fixed per format: a
// byte copy of W-3's small base, `base_doc()` and `orch_base_doc()` with their
// default arguments in tests/dictionary/overlay_refusal_test.cpp, so that a W-3
// overlay used as a seed meets the base its W-3 row was written for. The load
// goes through `XmlLoader::load_with_overlay_from_string` or
// `OrchestraLoader::load_with_overlay_from_string`, called through research
// R-5's read seam (src/dictionary/overlay_merge.hpp), on a bounded arena as in
// fuzz_orchestra_loader.cpp, so the `xml_oom_error` path is reachable.
//
// Invariants (a violation aborts, or rethrows so that libFuzzer records the input):
//   - A refusal from the build's step counter (the counter origin) aborts: the
//     guard stage's model of `finalize` drifted from the build.
//   - On every input that loads, the step counter must equal step 6's predicted
//     total, or the harness aborts.
//   - A refusal from the pre-walk budget or side-table fit is an accepted
//     `overlay_limit_error`.
//   - Only research R-11's exception set may escape the loader. QuickFIX XML:
//     `overlay_limit_error`, `overlay_conflict_error`,
//     `group_delimiter_collision_error`, `xml_parse_error`,
//     `unknown_version_error`, `xml_oom_error`. Orchestra: the same with
//     `orchestra_parse_error` in place of the bare `xml_parse_error`, caught by
//     its derived type, so a bare `xml_parse_error` from the Orchestra loader
//     is rethrown. A bare `std::bad_alloc` that is not `xml_oom_error`, and any
//     other exception, is rethrown.
//
// The base copies must stay byte-identical to W-3's: re-derive them by printing
// `base_doc()` and `orch_base_doc()` from overlay_refusal_test.cpp and comparing.
//
// Corpus on disk: tests/fuzz/corpus/dict_overlay/ (each seed is the format byte
// followed by an overlay).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory_resource>
#include <string_view>

#include "fixpp/dict/error.hpp"
#include "overlay_merge.hpp"

namespace {

namespace dict = fixpp::dict;
namespace ov = fixpp::dict::detail::overlay;

// Byte copy of W-3's `base_doc()` (overlay_refusal_test.cpp), default arguments.
constexpr std::string_view kXmlBase = R"B(<fix type='FIX' major='4' minor='4' servicepack='0'>
 <header>
  <field name='BeginString' required='Y' />
  <field name='BodyLength' required='Y' />
  <field name='MsgType' required='Y' />
 </header>
 <trailer>
  <field name='CheckSum' required='Y' />
 </trailer>
 <messages>
  <message name='Heartbeat' msgtype='0' msgcat='admin'>
   <field name='TestReqID' required='N' />
  </message>
  <message name='NewOrderSingle' msgtype='D' msgcat='app'>
   <field name='ClOrdID' required='Y' />
   <field name='Account' required='N' />
   <component name='Parties' required='N' />
   <component name='Instrument' required='Y' />
   <field name='Side' required='Y' />
   <group name='NoAllocs' required='N'>
    <field name='AllocAccount' required='N' />
   </group>
   <field name='Text' required='N' />
  </message>
  <message name='ExecutionReport' msgtype='8' msgcat='app'>
   <field name='OrderID' required='Y' />
   <component name='Outer' required='N' />
  </message>
 </messages>
 <components>
  <component name='Instrument'>
   <field name='Symbol' required='Y' />
   <field name='SecurityID' required='N' />
  </component>
  <component name='Parties'>
   <group name='NoPartyIDs' required='N'>
    <field name='PartyID' required='N' />
   </group>
  </component>
  <component name='Outer'>
   <component name='Parties' required='N' />
  </component>
 </components>
 <fields>
  <field number='1' name='Account' type='STRING' />
  <field number='8' name='BeginString' type='STRING' />
  <field number='9' name='BodyLength' type='LENGTH' />
  <field number='10' name='CheckSum' type='STRING' />
  <field number='11' name='ClOrdID' type='STRING' />
  <field number='35' name='MsgType' type='STRING' />
  <field number='37' name='OrderID' type='STRING' />
  <field number='48' name='SecurityID' type='STRING' />
  <field number='54' name='Side' type='CHAR'>
   <value enum='1' description='BUY' />
   <value enum='2' description='SELL' />
  </field>
  <field number='55' name='Symbol' type='STRING' />
  <field number='58' name='Text' type='STRING' />
  <field number='78' name='NoAllocs' type='NUMINGROUP' />
  <field number='79' name='AllocAccount' type='STRING' />
  <field number='112' name='TestReqID' type='STRING' />
  <field number='448' name='PartyID' type='STRING' />
  <field number='453' name='NoPartyIDs' type='NUMINGROUP' />
 </fields>
</fix>
)B";

// Byte copy of W-3's `orch_base_doc()` (overlay_refusal_test.cpp), default arguments.
constexpr std::string_view kOrchBase =
    R"B(<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' version='FIX.Latest_EP303'>
 <fixr:codeSets>
  <fixr:codeSet id='54' name='SideCodeSet' type='char'>
   <fixr:code id='54001' name='Buy' value='1' />
   <fixr:code id='54002' name='Sell' value='2' />
  </fixr:codeSet>
 </fixr:codeSets>
 <fixr:fields>
  <fixr:field id='1' name='Account' type='String' />
  <fixr:field id='8' name='BeginString' type='String' />
  <fixr:field id='9' name='BodyLength' type='Length' />
  <fixr:field id='10' name='CheckSum' type='String' />
  <fixr:field id='11' name='ClOrdID' type='String' />
  <fixr:field id='35' name='MsgType' type='String' />
  <fixr:field id='37' name='OrderID' type='String' />
  <fixr:field id='48' name='SecurityID' type='String' />
  <fixr:field id='54' name='Side' type='SideCodeSet' />
  <fixr:field id='55' name='Symbol' type='String' />
  <fixr:field id='58' name='Text' type='String' />
  <fixr:field id='78' name='NoAllocs' type='NumInGroup' />
  <fixr:field id='79' name='AllocAccount' type='String' />
  <fixr:field id='112' name='TestReqID' type='String' />
  <fixr:field id='448' name='PartyID' type='String' />
  <fixr:field id='453' name='NoPartyIDs' type='NumInGroup' />
 </fixr:fields>
 <fixr:components>
  <fixr:component id='1024' name='StandardHeader'>
    <fixr:fieldRef id='8' presence='required' />
    <fixr:fieldRef id='9' presence='required' />
    <fixr:fieldRef id='35' presence='required' />
  </fixr:component>
  <fixr:component id='1025' name='StandardTrailer'>
    <fixr:fieldRef id='10' presence='required' />
  </fixr:component>
  <fixr:component id='1003' name='Instrument'>
    <fixr:fieldRef id='55' presence='required' />
    <fixr:fieldRef id='48' />
  </fixr:component>
  <fixr:component id='2001' name='Parties'>
    <fixr:groupRef id='1012' />
  </fixr:component>
  <fixr:component id='2002' name='Outer'>
    <fixr:componentRef id='2001' />
  </fixr:component>
 </fixr:components>
 <fixr:groups>
  <fixr:group id='1012' name='PartyGroup'>
    <fixr:numInGroup id='453' />
    <fixr:fieldRef id='448' />
  </fixr:group>
  <fixr:group id='2010' name='Allocs'>
    <fixr:numInGroup id='78' />
    <fixr:fieldRef id='79' />
  </fixr:group>
 </fixr:groups>
 <fixr:messages>
  <fixr:message id='1' name='Heartbeat' msgType='0'>
   <fixr:structure>
    <fixr:componentRef id='1024' presence='required' />
    <fixr:fieldRef id='112' />
    <fixr:componentRef id='1025' presence='required' />
   </fixr:structure>
  </fixr:message>
  <fixr:message id='14' name='NewOrderSingle' msgType='D'>
   <fixr:structure>
    <fixr:componentRef id='1024' presence='required' />
    <fixr:fieldRef id='11' presence='required' />
    <fixr:fieldRef id='1' />
    <fixr:componentRef id='2001' />
    <fixr:componentRef id='1003' presence='required' />
    <fixr:fieldRef id='54' presence='required' />
    <fixr:groupRef id='2010' />
    <fixr:fieldRef id='58' />
    <fixr:componentRef id='1025' presence='required' />
   </fixr:structure>
  </fixr:message>
  <fixr:message id='9' name='ExecutionReport' msgType='8'>
   <fixr:structure>
    <fixr:componentRef id='1024' presence='required' />
    <fixr:fieldRef id='37' presence='required' />
    <fixr:componentRef id='2002' />
    <fixr:componentRef id='1025' presence='required' />
   </fixr:structure>
  </fixr:message>
 </fixr:messages>
</fixr:repository>
)B";

[[noreturn]] void fail(char const* why, ov::xml_overlay_probe const& p) {
    // NOLINTNEXTLINE(modernize-use-std-print): libstdc++ 13 has no <print>
    std::fprintf(stderr,
                 "fuzz_dict_overlay: %s (origin %d, counter %llu, predicted %llu, loaded %d)\n",
                 why, static_cast<int>(p.origin), static_cast<unsigned long long>(p.counter),
                 p.guards ? static_cast<unsigned long long>(p.guards->merged_total) : 0ULL,
                 p.dictionary.has_value() ? 1 : 0);
    std::abort();
}

// The seam's two checks (research R-5, the step counter).
void check_seam(ov::xml_overlay_probe const& p) {
    if (p.origin == ov::refusal_origin::counter) {
        fail("refused by the build's step counter (internal model drift)", p);
    }
    if (p.dictionary.has_value()) {
        if (!p.guards.has_value()) {
            fail("loaded without step 6's totals", p);
        }
        if (p.counter != p.guards->merged_total) {
            fail("loaded with a step counter that differs from step 6's prediction", p);
        }
    }
}

void accept_xml_refusal(std::exception_ptr const& e) {
    try {
        std::rethrow_exception(e);
    } catch (dict::overlay_limit_error const&) {
        // A cap, DOCTYPE, the budget or a side table (the counter origin aborted above).
    } catch (dict::overlay_conflict_error const&) {
        // R-4's conflicts and root checks.
    } catch (dict::group_delimiter_collision_error const&) {
        // A nested group's delimiter equals its parent's.
    } catch (dict::xml_parse_error const&) {
        // Malformed overlay, unknown reference, cycle, the loader's schema errors.
    } catch (dict::unknown_version_error const&) {
        // The base's version is outside the registry (not reachable with the fixed base).
    } catch (dict::xml_oom_error const&) {
        // Bounded-arena exhaustion, or pugixml's status_out_of_memory.
    }
    // Anything else, a bare std::bad_alloc included, propagates out of the
    // harness and libFuzzer records the input.
}

void accept_orchestra_refusal(std::exception_ptr const& e) {
    try {
        std::rethrow_exception(e);
    } catch (dict::overlay_limit_error const&) {
        // A cap, DOCTYPE, the budget or a side table (the counter origin aborted above).
    } catch (dict::overlay_conflict_error const&) {
        // R-4's conflicts and root checks.
    } catch (dict::group_delimiter_collision_error const&) {
        // A nested group's delimiter equals its parent's.
    } catch (dict::orchestra_parse_error const&) {
        // Malformed overlay, unknown reference, cycle, the loader's schema errors.
        // The derived type only: a bare xml_parse_error is not caught here.
    } catch (dict::unknown_version_error const&) {
        // The base's version is outside the registry (not reachable with the fixed base).
    } catch (dict::xml_oom_error const&) {
        // Bounded-arena exhaustion, or pugixml's status_out_of_memory.
    }
}

// The arena's backing store. Static rather than on the stack, so the stack is
// left to the loader's recursion; each input gets a fresh resource over it.
// Mutable by design: the arena writes into it.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
alignas(std::max_align_t) std::byte g_scratch[std::size_t{1} << 20U];

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) {
        return 0;
    }
    bool const orchestra = (data[0] & 1U) != 0U;
    std::string_view const overlay{reinterpret_cast<const char*>(data) + 1, size - 1};

    // Declared before the probe result, so the dictionary is destroyed first.
    std::pmr::monotonic_buffer_resource arena{g_scratch, sizeof g_scratch,
                                              std::pmr::null_memory_resource()};
    ov::xml_overlay_probe const p =
        orchestra ? ov::probe_orchestra_overlay_from_string(kOrchBase, overlay, &arena)
                  : ov::probe_xml_overlay_from_string(kXmlBase, overlay, &arena);

    check_seam(p);
    if (p.dictionary.has_value()) {
        (void)p.dictionary->which_session_version();
    }
    if (p.error) {
        if (orchestra) {
            accept_orchestra_refusal(p.error);
        } else {
            accept_xml_refusal(p.error);
        }
    }
    return 0;
}
