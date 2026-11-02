// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/overlay_pugi_oom_test.cpp — 098-dialect-overlay: the splice under
// a failing pugixml allocator (contracts/witnesses.md "W-9 splice allocation
// failure"; research R-6).
//
// The overlay splice (src/dictionary/overlay_merge.cpp, `copy_element_if` and both
// formats' Required promotion) mutates the base document through pugixml, which
// reports a failed allocation by returning a null handle or `false`, never by
// throwing. Each failure must become `std::bad_alloc`, which the public entries turn
// into `xml_oom_error`. Two shapes of failure are in play:
//   - the object itself does not fit: the mutator returns a null handle;
//   - the object fits and its NAME does not: pugixml's named mutators
//     (`append_child(name)`, `append_attribute(name)`) discard `set_name`'s result and
//     return a live, nameless handle already linked into the tree. Both loaders look
//     elements and attributes up by name, so a nameless one is silently ignored.
//
// Mechanism: `pugi::set_memory_management_functions`, installed once as `main`'s
// first statement, before any pugixml object exists, and never removed, so every
// page is freed by the function matching the one that allocated it. The hook
// forwards to `std::malloc`/`std::free`; a mode flag makes it return null. pugixml
// calls the hook only for a new page, so a cell first consumes the base document's
// current page through a scratch node outside the root, down to an exact remainder,
// swept in pugixml's 8-byte block steps from zero past the splice's whole size. The hook is
// process-global and unsynchronized, so this is a standalone, single-threaded
// executable ([const §VII.8]'s isolation-sensitive carve-out). It forwards to
// malloc, so it runs under the sanitizers too.
//
// Arms:
//   - PositiveControl: the hook sees the library's pugixml (a clean overlay load
//     calls it), and failing it makes an overlay load throw `xml_oom_error`. Without
//     both, every other arm would read green.
//   - SpliceUnderPersistentFailure (S1 QuickFIX, S2 Orchestra): every new page fails
//     from the splice on. Oracle: a run whose splice met a failed allocation throws
//     `std::bad_alloc`, and a run that returns leaves the base's root serialized
//     byte-equal to a splice with no failure. A straddle check requires both
//     outcomes across the sweep, else it never crossed the splice's allocations.
//   - SpliceUnderTransientFailure (S3): exactly the n-th new page of the splice fails
//     and later ones succeed, for every n up to one past the splice's own count, from
//     every remainder. A nameless node or attribute then stays in the tree instead of
//     being masked by a later failure. Same oracle.
//   - PublicEntryComposition (P): the public string entries with the n-th allocation
//     failing, for every n over a clean call, each throw exactly `xml_oom_error`; one
//     past the count, the load equals the edited reference. Parse failures satisfy
//     this too, so it witnesses composition, not the splice checks.
//
// Mutation procedure: in a copy of the tree, restore a named mutator at one splice
// site or delete one `throw std::bad_alloc()` there, rebuild this binary, and run it
// unfiltered; the arm the site belongs to must fail with a divergent success.
//
// Research R-6 and plan A-006 declined an allocation-failure seam for the C ABI's own
// allocations; that decision stands. This executable is the seam for the splice.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <memory_resource>
#include <new>
#include <pugixml.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <typeinfo>

#include "dictionary_equivalence.hpp"
#include "overlay_merge.hpp"
#include "overlay_test_support.hpp"

namespace {

namespace eq = fixpp::dict::equivalence;
namespace ot = overlay_test;
namespace ov = fixpp::dict::detail::overlay;
using fixpp::dict::Dictionary;
using fixpp::dict::OrchestraLoader;
using fixpp::dict::xml_oom_error;
using fixpp::dict::XmlLoader;

// ── the allocator hook ───────────────────────────────────────────────────────

enum class fail_mode : std::uint8_t { off, every_call, nth_call };

// Read by the hook; the executable is single-threaded.
struct hook_state {
    fail_mode mode = fail_mode::off;
    std::size_t calls = 0;    // hook calls since the last `arm`
    std::size_t fail_at = 0;  // nth_call: the 1-based call that fails
};

hook_state& hook() {
    static hook_state s;
    return s;
}

void* hook_allocate(std::size_t size) {
    hook_state& h = hook();
    ++h.calls;
    if (h.mode == fail_mode::every_call ||
        (h.mode == fail_mode::nth_call && h.calls == h.fail_at)) {
        return nullptr;
    }
    // pugixml's hook contract is malloc and free.
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc,cppcoreguidelines-owning-memory)
    return std::malloc(size);
}

// pugixml's hook contract is malloc and free.
// NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc,cppcoreguidelines-owning-memory)
void hook_deallocate(void* p) { std::free(p); }

void arm(fail_mode m, std::size_t nth = 0) {
    hook() = hook_state{.mode = m, .calls = 0, .fail_at = nth};
}

std::pmr::memory_resource* mr() { return std::pmr::new_delete_resource(); }

// ── documents ────────────────────────────────────────────────────────────────

// `account` is NewOrderSingle's reference to Account.
std::string qf_base(std::string_view account = "   <field name='Account' />\n") {
    return "<fix type='FIX' major='4' minor='4' servicepack='0'>\n"
           " <header>\n"
           "  <field name='BeginString' required='Y' />\n"
           "  <field name='BodyLength' required='Y' />\n"
           "  <field name='MsgType' required='Y' />\n"
           " </header>\n"
           " <messages>\n"
           "  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n"
           "   <field name='ClOrdID' required='Y' />\n" +
           std::string{account} +
           "  </message>\n"
           " </messages>\n"
           " <components>\n"
           " </components>\n"
           " <fields>\n"
           "  <field number='1' name='Account' type='STRING' />\n"
           "  <field number='8' name='BeginString' type='STRING' />\n"
           "  <field number='9' name='BodyLength' type='LENGTH' />\n"
           "  <field number='10' name='CheckSum' type='STRING' />\n"
           "  <field number='11' name='ClOrdID' type='STRING' />\n"
           "  <field number='35' name='MsgType' type='STRING' />\n"
           "  <field number='58' name='Text' type='STRING' />\n"
           " </fields>\n"
           "</fix>\n";
}

std::string qf_overlay(std::string_view body) {
    return "<fix type='FIX' major='4' minor='4' servicepack='0'>\n" + std::string{body} +
           "</fix>\n";
}

constexpr std::string_view kOrchOpen =
    "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' "
    "version='FIX.Latest_EP303'>\n";

// No <fixr:groups>, so an overlay's groups container is one the base lacks.
std::string orch_base() {
    return std::string{kOrchOpen} +
           " <fixr:fields>\n"
           "  <fixr:field id='1' name='Account' type='String' />\n"
           "  <fixr:field id='8' name='BeginString' type='String' />\n"
           "  <fixr:field id='9' name='BodyLength' type='Length' />\n"
           "  <fixr:field id='10' name='CheckSum' type='String' />\n"
           "  <fixr:field id='11' name='ClOrdID' type='String' />\n"
           "  <fixr:field id='35' name='MsgType' type='String' />\n"
           "  <fixr:field id='58' name='Text' type='String' />\n"
           "  <fixr:field id='78' name='NoAllocs' type='NumInGroup' />\n"
           "  <fixr:field id='79' name='AllocAccount' type='String' />\n"
           " </fixr:fields>\n"
           " <fixr:components>\n"
           "  <fixr:component id='1024' name='StandardHeader'>\n"
           "    <fixr:fieldRef id='8' presence='required' />\n"
           "    <fixr:fieldRef id='9' presence='required' />\n"
           "    <fixr:fieldRef id='35' presence='required' />\n"
           "  </fixr:component>\n"
           "  <fixr:component id='1025' name='StandardTrailer'>\n"
           "    <fixr:fieldRef id='10' presence='required' />\n"
           "  </fixr:component>\n"
           " </fixr:components>\n"
           " <fixr:messages>\n"
           "  <fixr:message id='14' name='NewOrderSingle' msgType='D'>\n"
           "   <fixr:structure>\n"
           "    <fixr:componentRef id='1024' presence='required' />\n"
           "    <fixr:fieldRef id='11' presence='required' />\n"
           "    <fixr:fieldRef id='1' />\n"
           "    <fixr:componentRef id='1025' presence='required' />\n"
           "   </fixr:structure>\n"
           "  </fixr:message>\n"
           " </fixr:messages>\n"
           "</fixr:repository>\n";
}

std::string orch_overlay(std::string_view body) {
    return std::string{kOrchOpen} + std::string{body} + "</fixr:repository>\n";
}

std::string orch_extend_d(std::string_view refs) {
    return " <fixr:messages>\n"
           "  <fixr:message id='14' name='NewOrderSingle' msgType='D'>\n"
           "   <fixr:structure>\n" +
           std::string{refs} +
           "   </fixr:structure>\n"
           "  </fixr:message>\n"
           " </fixr:messages>\n";
}

enum class fmt : std::uint8_t { quickfix_xml, orchestra };

struct fixture {
    char const* name = "";
    fmt format = fmt::quickfix_xml;
    std::string base;
    std::string overlay;
};

// QuickFIX: (a) a promotion only, the attribute absent in the base; (a') the same
// promotion over an empty `required` in the base, which the new value cannot reuse;
// (b) one appended reference whose last allocation is `required="Y"`, a name block
// larger than its value block; (c) a root container the base lacks, an element with
// no attributes.
fixture qf_promotion() {
    return {.name = "qf promotion",
            .format = fmt::quickfix_xml,
            .base = qf_base(),
            .overlay = qf_overlay(" <messages>\n"
                                  "  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n"
                                  "   <field name='Account' required='Y' />\n"
                                  "  </message>\n"
                                  " </messages>\n")};
}
fixture qf_promotion_over_empty() {
    return {.name = "qf promotion over an empty attribute",
            .format = fmt::quickfix_xml,
            .base = qf_base("   <field name='Account' required='' />\n"),
            .overlay = qf_overlay(" <messages>\n"
                                  "  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n"
                                  "   <field name='Account' required='Y' />\n"
                                  "  </message>\n"
                                  " </messages>\n")};
}
fixture qf_append() {
    return {.name = "qf appended reference",
            .format = fmt::quickfix_xml,
            .base = qf_base(),
            .overlay = qf_overlay(" <messages>\n"
                                  "  <message name='NewOrderSingle' msgtype='D' msgcat='app'>\n"
                                  "   <field name='Text' required='Y' />\n"
                                  "  </message>\n"
                                  " </messages>\n")};
}
fixture qf_container() {
    return {.name = "qf container the base lacks",
            .format = fmt::quickfix_xml,
            .base = qf_base(),
            .overlay = qf_overlay(" <trailer>\n"
                                  "  <field name='CheckSum' required='Y' />\n"
                                  " </trailer>\n")};
}

// Orchestra: a `presence="required"` promotion; a copied `fixr:field` and the
// reference to it; a root container the base lacks.
fixture orch_promotion() {
    return {.name = "orch promotion",
            .format = fmt::orchestra,
            .base = orch_base(),
            .overlay =
                orch_overlay(orch_extend_d("    <fixr:fieldRef id='1' presence='required' />\n"))};
}
fixture orch_copy() {
    return {.name = "orch copied field",
            .format = fmt::orchestra,
            .base = orch_base(),
            .overlay = orch_overlay(" <fixr:fields>\n"
                                    "  <fixr:field id='9001' name='VenueQty' type='Qty' />\n"
                                    " </fixr:fields>\n" +
                                    orch_extend_d("    <fixr:fieldRef id='9001' />\n"))};
}
fixture orch_container() {
    return {.name = "orch container the base lacks",
            .format = fmt::orchestra,
            .base = orch_base(),
            .overlay = orch_overlay(" <fixr:groups>\n"
                                    "  <fixr:group id='2010' name='Allocs'>\n"
                                    "    <fixr:numInGroup id='78' />\n"
                                    "    <fixr:fieldRef id='79' />\n"
                                    "  </fixr:group>\n"
                                    " </fixr:groups>\n")};
}

// ── one splice ───────────────────────────────────────────────────────────────

// The remainders swept, in bytes: past the largest fixture's whole splice, so the
// last ones let every splice fit.
constexpr std::size_t kMaxRemainder = 1024;

// pugixml rounds every block of a page to this, so a page's remainder is a multiple
// of it.
constexpr std::size_t kBlock = 8;

// Consumes 8-byte string blocks of the document's current page through `slot`, with
// every new page failing: at most `limit` of them, or until one does not fit. Each
// round frees the slot's block and allocates a fresh one, and pugixml never reuses
// freed space inside a page. Returns how many it consumed.
std::size_t consume_blocks(pugi::xml_attribute slot, std::size_t limit) {
    arm(fail_mode::every_call);
    std::size_t k = 0;
    while (k < limit && slot.set_value("") && slot.set_value("x")) {
        ++k;
    }
    return k;
}

std::string serialized(pugi::xml_document const& doc) {
    std::ostringstream os;
    doc.document_element().print(os, "", pugi::format_raw);
    return os.str();
}

struct splice_result {
    bool threw = false;     // the splice threw std::bad_alloc
    bool injected = false;  // the hook returned null at least once during the splice
    std::string text;       // the base's root after the splice, when it returned
};

// No fill: the splice starts from wherever the base's page stands.
constexpr std::size_t kNoFill = static_cast<std::size_t>(-1);
// Fill until the page is full, and report the block count instead of splicing.
constexpr std::size_t kMeasure = static_cast<std::size_t>(-2);

// Parses `f`, runs the guard with no failure, appends a scratch node outside the root
// with one attribute, and then, unless `remainder` is kNoFill, consumes the page down
// to exactly `remainder` bytes (`full` is the block count that fills it), arms the hook
// and splices. The scratch node is removed before the root is serialized.
template <class Merge>
splice_result splice_with(fixture const& f, std::size_t remainder, std::size_t full, fail_mode m,
                          std::size_t nth, std::size_t* measured = nullptr) {
    arm(fail_mode::off);
    pugi::xml_document base;
    pugi::xml_document overlay;
    EXPECT_TRUE(base.load_buffer(f.base.data(), f.base.size())) << f.name;
    EXPECT_TRUE(overlay.load_buffer(f.overlay.data(), f.overlay.size(),
                                    pugi::parse_default | pugi::parse_doctype))
        << f.name;
    Merge merge{base, overlay};
    ov::overlay_trace trace;
    (void)merge.guard(trace);
    pugi::xml_node scratch = base.append_child("scratch");
    pugi::xml_attribute const slot = scratch.append_attribute("");
    EXPECT_TRUE(scratch && slot) << f.name;
    splice_result r;
    if (remainder == kMeasure) {
        *measured = consume_blocks(slot, kNoFill);
        arm(fail_mode::off);
        return r;
    }
    if (remainder != kNoFill) {
        std::size_t const want = full - (remainder / kBlock);
        EXPECT_EQ(consume_blocks(slot, want), want) << f.name << " remainder " << remainder;
    }
    arm(m, nth);
    try {
        merge.splice();
    } catch (std::bad_alloc const&) {
        r.threw = true;
    }
    r.injected = m == fail_mode::every_call ? hook().calls > 0
                                            : (m == fail_mode::nth_call && hook().calls >= nth);
    arm(fail_mode::off);
    base.remove_child(scratch);
    if (!r.threw) {
        r.text = serialized(base);
    }
    return r;
}

splice_result splice_of(fixture const& f, std::size_t remainder, std::size_t full, fail_mode m,
                        std::size_t nth = 0) {
    return f.format == fmt::orchestra
               ? splice_with<ov::orchestra_overlay_merge>(f, remainder, full, m, nth)
               : splice_with<ov::xml_overlay_merge>(f, remainder, full, m, nth);
}

// How many 8-byte blocks fill the page `splice_with` leaves the splice; every step
// before the fill is deterministic, so the count holds for every run of `f`.
std::size_t full_page_blocks(fixture const& f) {
    std::size_t n = 0;
    if (f.format == fmt::orchestra) {
        (void)splice_with<ov::orchestra_overlay_merge>(f, kMeasure, 0, fail_mode::off, 0, &n);
    } else {
        (void)splice_with<ov::xml_overlay_merge>(f, kMeasure, 0, fail_mode::off, 0, &n);
    }
    return n;
}

// The fixture loads through the public entry with no failure, so its splice is one
// the guard admits and the build accepts.
void expect_admissible(fixture const& f) {
    arm(fail_mode::off);
    EXPECT_NO_THROW({
        Dictionary const d =
            f.format == fmt::orchestra
                ? OrchestraLoader{}.load_with_overlay_from_string(f.base, f.overlay, mr())
                : XmlLoader{}.load_with_overlay_from_string(f.base, f.overlay, mr());
        (void)d;
    }) << f.name;
}

struct tally {
    int runs = 0;
    int threw = 0;
    int equal = 0;
    int divergent = 0;
    int injected = 0;
    long first_divergent_remainder = -1;
    std::string first_divergent_text;
};

void count(tally& t, splice_result const& r, std::string const& reference, std::size_t remainder) {
    ++t.runs;
    t.injected += r.injected ? 1 : 0;
    if (r.threw) {
        ++t.threw;
    } else if (r.text == reference) {
        ++t.equal;
    } else {
        ++t.divergent;
        if (t.first_divergent_remainder < 0) {
            t.first_divergent_remainder = static_cast<long>(remainder);
            t.first_divergent_text = r.text;
        }
    }
}

void print(char const* arm_name, fixture const& f, tally const& t) {
    std::cout << "[098 pugi-oom " << arm_name << " | " << f.name << "] runs " << t.runs
              << ", bad_alloc " << t.threw << ", equal " << t.equal << ", divergent " << t.divergent
              << ", injected " << t.injected << ", first divergent remainder "
              << t.first_divergent_remainder << '\n';
}

// The splice of `f` with no failure, and the block count that fills its page; the
// page must hold at least kMaxRemainder bytes for the sweep to reach every remainder.
struct sweep_basis {
    std::string reference;
    std::size_t full = 0;
};

sweep_basis basis_of(fixture const& f) {
    expect_admissible(f);
    splice_result const ref = splice_of(f, kNoFill, 0, fail_mode::off);
    EXPECT_FALSE(ref.threw) << f.name;
    sweep_basis b{.reference = ref.text, .full = full_page_blocks(f)};
    EXPECT_GE(b.full, kMaxRemainder / kBlock) << f.name;
    return b;
}

// S1 / S2: every new page fails from the splice on, from every remainder.
void persistent_sweep(fixture const& f) {
    sweep_basis const b = basis_of(f);
    ASSERT_GE(b.full, kMaxRemainder / kBlock) << f.name;
    tally t;
    for (std::size_t rem = 0; rem <= kMaxRemainder; rem += kBlock) {
        count(t, splice_of(f, rem, b.full, fail_mode::every_call), b.reference, rem);
    }
    print("persistent", f, t);
    EXPECT_EQ(t.divergent, 0) << f.name
                              << ": a splice under allocation failure returned a document "
                                 "that differs from the splice without failure; reference:\n"
                              << b.reference << "\nfirst divergent:\n"
                              << t.first_divergent_text;
    EXPECT_GT(t.threw, 0) << f.name
                          << ": no remainder made the splice fail; the sweep never "
                             "reached the splice's allocations";
    EXPECT_EQ(t.threw, t.injected)
        << f.name << ": a splice that met a failed allocation returned instead of throwing";
    EXPECT_GT(t.equal, 0) << f.name
                          << ": no remainder let the splice fit; the sweep never "
                             "crossed the splice's allocations";
}

// S3: exactly the n-th new page of the splice fails and later ones succeed, for every
// n up to one past the splice's own count, from every remainder.
void transient_sweep(fixture const& f) {
    sweep_basis const b = basis_of(f);
    ASSERT_GE(b.full, kMaxRemainder / kBlock) << f.name;
    constexpr std::size_t kMaxN = 64;
    tally t;
    for (std::size_t rem = 0; rem <= kMaxRemainder; rem += kBlock) {
        for (std::size_t n = 1; n <= kMaxN; ++n) {
            splice_result const r = splice_of(f, rem, b.full, fail_mode::nth_call, n);
            count(t, r, b.reference, rem);
            if (!r.injected) {
                break;  // n is one past the splice's own count
            }
            ASSERT_LT(n, kMaxN) << f.name << ": the splice made more than " << kMaxN
                                << " page requests";
        }
    }
    print("transient", f, t);
    EXPECT_EQ(t.divergent, 0) << f.name
                              << ": a splice with one failed allocation returned a document "
                                 "that differs from the splice without failure; reference:\n"
                              << b.reference << "\nfirst divergent:\n"
                              << t.first_divergent_text;
    EXPECT_GT(t.injected, 0) << f.name << ": no run injected a failure into the splice";
    EXPECT_EQ(t.threw, t.injected)
        << f.name << ": a splice that met a failed allocation returned instead of throwing";
}

// ── the arms ─────────────────────────────────────────────────────────────────

TEST(OverlayPugiOomPositiveControl, TheHookSeesTheLibrarysPugixml) {
    std::string const base = ot::read_file(ot::fixture_dir() / "qf" / "base.xml");
    std::string const overlay = ot::read_file(ot::fixture_dir() / "qf" / "overlay.xml");
    arm(fail_mode::off);
    Dictionary const d = XmlLoader{}.load_with_overlay_from_string(base, overlay, mr());
    (void)d;
    EXPECT_GT(hook().calls, 0U) << "a clean overlay load never called the hook";
}

TEST(OverlayPugiOomPositiveControl, AFailingHookMakesAnOverlayLoadThrowXmlOomError) {
    std::string const base = ot::read_file(ot::fixture_dir() / "qf" / "base.xml");
    std::string const overlay = ot::read_file(ot::fixture_dir() / "qf" / "overlay.xml");
    std::string thrown = "nothing thrown";
    arm(fail_mode::every_call);
    try {
        Dictionary const d = XmlLoader{}.load_with_overlay_from_string(base, overlay, mr());
        (void)d;
    } catch (std::exception const& e) {
        thrown = typeid(e).name();
    }
    arm(fail_mode::off);
    EXPECT_EQ(thrown, typeid(xml_oom_error).name());
}

TEST(OverlayPugiOomSpliceUnderPersistentFailure, QfPromotion) { persistent_sweep(qf_promotion()); }
TEST(OverlayPugiOomSpliceUnderPersistentFailure, QfPromotionOverAnEmptyAttribute) {
    persistent_sweep(qf_promotion_over_empty());
}
TEST(OverlayPugiOomSpliceUnderPersistentFailure, QfAppendedReference) {
    persistent_sweep(qf_append());
}
TEST(OverlayPugiOomSpliceUnderPersistentFailure, QfContainerTheBaseLacks) {
    persistent_sweep(qf_container());
}
TEST(OverlayPugiOomSpliceUnderPersistentFailure, OrchPromotion) {
    persistent_sweep(orch_promotion());
}
TEST(OverlayPugiOomSpliceUnderPersistentFailure, OrchCopiedField) { persistent_sweep(orch_copy()); }
TEST(OverlayPugiOomSpliceUnderPersistentFailure, OrchContainerTheBaseLacks) {
    persistent_sweep(orch_container());
}

TEST(OverlayPugiOomSpliceUnderTransientFailure, QfPromotion) { transient_sweep(qf_promotion()); }
TEST(OverlayPugiOomSpliceUnderTransientFailure, QfPromotionOverAnEmptyAttribute) {
    transient_sweep(qf_promotion_over_empty());
}
TEST(OverlayPugiOomSpliceUnderTransientFailure, QfAppendedReference) {
    transient_sweep(qf_append());
}
TEST(OverlayPugiOomSpliceUnderTransientFailure, QfContainerTheBaseLacks) {
    transient_sweep(qf_container());
}
TEST(OverlayPugiOomSpliceUnderTransientFailure, OrchPromotion) {
    transient_sweep(orch_promotion());
}
TEST(OverlayPugiOomSpliceUnderTransientFailure, OrchCopiedField) { transient_sweep(orch_copy()); }
TEST(OverlayPugiOomSpliceUnderTransientFailure, OrchContainerTheBaseLacks) {
    transient_sweep(orch_container());
}

// P: composition through the public string entries. Not a witness of the splice
// checks: a failure that lands in either parse satisfies it as well.
void composition_sweep(fmt format) {
    std::filesystem::path const dir =
        ot::fixture_dir() / (format == fmt::orchestra ? "orch" : "qf");
    std::string const base = ot::read_file(dir / "base.xml");
    std::string const overlay = ot::read_file(dir / "overlay.xml");
    std::string const edited = ot::read_file(dir / "edited.xml");
    auto const load = [&] {
        return format == fmt::orchestra
                   ? OrchestraLoader{}.load_with_overlay_from_string(base, overlay, mr())
                   : XmlLoader{}.load_with_overlay_from_string(base, overlay, mr());
    };
    arm(fail_mode::off);
    (void)load();
    std::size_t const clean = hook().calls;
    ASSERT_GT(clean, 0U);
    for (std::size_t n = 1; n <= clean; ++n) {
        std::string thrown = "nothing thrown";
        arm(fail_mode::nth_call, n);
        try {
            (void)load();
        } catch (std::exception const& e) {
            thrown = typeid(e).name();
        }
        arm(fail_mode::off);
        EXPECT_EQ(thrown, typeid(xml_oom_error).name()) << "allocation " << n << " of " << clean;
    }
    arm(fail_mode::nth_call, clean + 1);
    Dictionary const got = load();
    arm(fail_mode::off);
    Dictionary const reference = format == fmt::orchestra
                                     ? OrchestraLoader{}.load_from_string(edited, mr())
                                     : XmlLoader{}.load_from_string(edited, mr());
    eq::raw_census const census = format == fmt::orchestra ? eq::census_from_orchestra(edited)
                                                           : eq::census_from_quickfix_xml(edited);
    eq::comparison_report const r = eq::compare(eq::side{.dict = got, .census = census},
                                                eq::side{.dict = reference, .census = census});
    EXPECT_TRUE(r.differences.empty()) << r.describe();
    EXPECT_TRUE(r.invariant_violations.empty()) << r.describe();
    std::cout << "[098 pugi-oom composition | " << (format == fmt::orchestra ? "orch" : "qf")
              << "] allocations " << clean << '\n';
}

TEST(OverlayPugiOomPublicEntryComposition, QuickFixXml) { composition_sweep(fmt::quickfix_xml); }
TEST(OverlayPugiOomPublicEntryComposition, Orchestra) { composition_sweep(fmt::orchestra); }

}  // namespace

int main(int argc, char** argv) {
    // Before any pugixml object exists, and never removed: a page must be freed by the
    // deallocator matching the allocator that made it.
    pugi::set_memory_management_functions(hook_allocate, hook_deallocate);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
