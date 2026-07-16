// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/vlatest_manifest_class_consistency_test.cpp
//
// 076-fix-latest-typed-codegen T014 [US2] -- V-1b manifest<->class
// consistency gate (specs/076-fix-latest-typed-codegen/tasks.md T014;
// FR-006/SC-001, INV-6, contract V-1b).
//
// This is the SECOND, DISTINCT completeness leg. V-1 (T013,
// vlatest_completeness_census_test.cpp) already pins
// manifest == raw-XML. This test pins manifest == shipped-read-class.
// Composing V-1 with V-1b (this test) pins class == raw-XML,
// non-circularly, because the two legs use two DIFFERENT derivations of
// "what the class contains":
//   * CLASS SIDE: parsed directly from the text of the SHIPPED generated
//     header build/<preset>/_codegen/include/fixpp/vlatest/Messages.hpp
//     (the header a consumer actually compiles against). No fixpp header
//     is included by this test and NO runtime Dictionary/table_view is
//     consulted -- the class-reachable field set is derived purely from
//     Messages.hpp's own C++ text (class bodies, `view_.template get<N>`
//     / `::fixpp::wire::get(ctx_.span, N, ...)` accessor calls, and
//     `group_view<...G_N>` return types that mark a nested-group
//     reference).
//   * MANIFEST SIDE: parsed from the T012-emitted Manifest.txt and
//     RE-PROJECTED (by this test, not sourced from the emitter) down to
//     the same "class-reachable" granularity (see PROJECTION RULE below).
// This implementer did NOT read tools/codegen/fixpp-codegen/ir.cpp or
// emit_manifest.cpp while writing this test (T014 brief, non-circularity
// constraint) -- only Messages.hpp and Manifest.txt were read to derive
// the extraction/projection rules below, both confirmed empirically
// against the real generated files (e.g. Heartbeat msg_type "0" and
// NewOrderSingle msg_type "D" / NoPartyIDs(453) / NoPartySubIDs(802)).
//
// CLASS-SIDE EXTRACTION RULE
// ---------------------------
// Messages.hpp has two disjoint class populations, distinguished purely
// by source-line indentation (verified: line counts of the two opening
// patterns below match the closing-brace-line counts and the
// `static constexpr ... msg_type_v` count exactly -- 181 message classes,
// 524 group flyweights):
//   - a MESSAGE class: `^class <Name> {$` (0-indent) ... `^};$` (0-indent)
//     body. Its top-level scalar accessors call `view_.template get<N>()`
//     (N = a field tag). A top-level repeating-group accessor's return
//     type is `...group_view<[::fixpp::vlatest::groups::]G_<N>>` (N ==
//     the group's NumInGroup counter tag -- verified: the flyweight class
//     is *named* by its own counter tag, e.g. `G_627` is Hops' NoHops(627)
//     flyweight, `G_802` is NoPartySubIDs(802)).
//   - a GROUP flyweight class: `^    class G_<N> {$` (4-indent) ...
//     `^    };$` (4-indent) body, in `namespace fixpp::vlatest::groups`.
//     Its top-level scalar accessors call
//     `::fixpp::wire::get(ctx_.span, N, ctx_.gen)`. A NESTED group
//     reference (e.g. PartyIDs(453) -> PartySubIDs(802)) uses the same
//     `group_view<...G_<N>>` return-type marker as the message-level case.
//
// class-reachable(M) = { N : `view_.template get<N>()` appears in M's
//   body } UNION { N : `group_view<...G_N>` appears in M's body } UNION,
//   transitively for each such N, class-reachable-group(N), where
//   class-reachable-group(G) = { N : `wire::get(ctx_.span, N,` in G's
//   body } UNION { N : `group_view<...G_N>` in G's body } UNION nested
//   closures. Because G_<N> is a single, version-wide-shared flyweight
//   (one definition, referenced by every message that uses group N), this
//   closure IS the "version-wide union" the brief asks for -- there is
//   exactly one G_<N> definition to close over, not a per-message one.
//
// MANIFEST-SIDE PROJECTION RULE
// -------------------------------
// Manifest.txt's OCC rows carry the FULL lossless (group_path, tag) chain
// (grammar: see Manifest.txt's own header comment). group_path "-" =
// top-level; "453" = directly inside group 453; "453.802" = inside group
// 802 nested inside group 453. The group a row belongs to is unambiguous:
// it is the LAST dot-separated segment of its group_path (or the whole
// segment, if there is no dot). This test computes, purely from
// Manifest.txt (never from the class side):
//   member_tags(G) = { tag : some OCC row's group_path's LAST segment
//                      == G }, unioned across ALL messages in the
//                      manifest (the manifest-side analog of "version-
//                      wide union" -- every message that uses group G
//                      contributes to the SAME member_tags(G), and since
//                      a given group id denotes one Orchestra <group>
//                      definition, every contributor agrees).
//   manifest-reachable(M) = { tag : OCC row for M with group_path "-" }
//     UNION, transitively for each such top-level tag T that is itself a
//     group id (member_tags(T) is non-empty), member_tags(T).
// This is a pure re-derivation of Manifest.txt's own grammar; it shares
// no code or data with the class-side extraction above, and the group-id
// "is this tag a group counter" test is answered independently on each
// side (class side: does `group_view<...G_N>` appear; manifest side:
// does member_tags(N) exist) -- confirmed to agree by the exact-set
// assertion below, not assumed.
//
// PRIMARY HARD GATE: message-set exact equality (181==181, both
// directions) AND, per message, class-reachable(M) == manifest-
// reachable(M) as an exact set (symmetric difference reported on
// failure, both directions, with msg_type + tag list).
//
// MUTATION-RED (see the T014 implementation report for the pasted
// proof): (1) delete one `view_.template get<N>()` (or group-body
// `wire::get(ctx_.span, N,`) accessor line from Messages.hpp in the
// build tree -> a tag drops out of class-reachable(M) -> per-message
// mismatch RED; (2) delete/rename one message class's msg_type_v line
// (or the whole class) -> message-set RED. Both are DISTINCT from V-1's
// raw-XML/manifest mutations (T013) and from the FR-007 round-trip (that
// gate is circular w.r.t. an absent accessor: a field the class never
// exposes is never round-tripped through, so it can't fail there).
//
// Anchors: specs/076-fix-latest-typed-codegen/tasks.md T014;
//          specs/076-fix-latest-typed-codegen/spec.md FR-006/SC-001,
//          INV-6; contracts/ V-1b.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef FIXPP_CODEGEN_VLATEST_MANIFEST
#error "FIXPP_CODEGEN_VLATEST_MANIFEST must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_VLATEST_MESSAGES_HPP
#error "FIXPP_CODEGEN_VLATEST_MESSAGES_HPP must be set by CMake target_compile_definitions"
#endif

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) lines.push_back(line);
  return lines;
}

std::set<int> find_all_ints(const std::string& body, const std::regex& re) {
  std::set<int> out;
  for (auto it = std::sregex_iterator(body.begin(), body.end(), re); it != std::sregex_iterator(); ++it) {
    out.insert(std::stoi((*it)[1].str()));
  }
  return out;
}

// ---------------------------- class side ----------------------------

struct ClassSide {
  std::set<std::string> msg_types;
  std::map<std::string, std::set<int>> reachable;  // msg_type -> tags
};

ClassSide parse_class_side(const std::string& path) {
  const std::vector<std::string> lines = split_lines(read_file(path));
  static const std::regex kMsgClassStart(R"(^class (\w+) \{$)");
  static const std::regex kGrpClassStart(R"(^    class G_(\d+) \{$)");
  static const std::regex kMsgTypeRe(R"re(static constexpr ::std::string_view msg_type_v = "([^"]*)";)re");
  static const std::regex kMsgTagRe(R"(view_\.template get<(\d+)>)");
  static const std::regex kGrpTagRe(R"(::fixpp::wire::get\(ctx_\.span,\s*(\d+),)");
  static const std::regex kGrpRefRe(R"(group_view<(?:::fixpp::vlatest::groups::)?G_(\d+)>)");

  std::map<int, std::string> group_body;
  std::map<std::string, std::string> msg_body;

  const size_t n = lines.size();
  for (size_t i = 0; i < n; ++i) {
    std::smatch m;
    if (std::regex_match(lines[i], m, kGrpClassStart)) {
      const int gid = std::stoi(m[1].str());
      size_t j = i + 1;
      std::string body;
      while (j < n && lines[j] != "    };") { body += lines[j]; body += '\n'; ++j; }
      if (j >= n) throw std::runtime_error("unterminated group class G_" + std::to_string(gid));
      group_body[gid] = std::move(body);
      i = j;
    } else if (std::regex_match(lines[i], m, kMsgClassStart)) {
      const std::string name = m[1].str();
      size_t j = i + 1;
      std::string body;
      while (j < n && lines[j] != "};") { body += lines[j]; body += '\n'; ++j; }
      if (j >= n) throw std::runtime_error("unterminated message class " + name);
      msg_body[name] = std::move(body);
      i = j;
    }
  }

  std::map<int, std::set<int>> cache;
  std::set<int> in_progress;
  std::function<const std::set<int>&(int)> closure = [&](int gid) -> const std::set<int>& {
    auto cached = cache.find(gid);
    if (cached != cache.end()) return cached->second;
    if (in_progress.count(gid)) throw std::runtime_error("class-side group cycle at G_" + std::to_string(gid));
    auto bit = group_body.find(gid);
    if (bit == group_body.end()) throw std::runtime_error("group_view references undefined G_" + std::to_string(gid));
    in_progress.insert(gid);
    std::set<int> result = find_all_ints(bit->second, kGrpTagRe);
    const std::set<int> refs = find_all_ints(bit->second, kGrpRefRe);
    result.insert(refs.begin(), refs.end());
    for (int r : refs) {
      const std::set<int>& sub = closure(r);
      result.insert(sub.begin(), sub.end());
    }
    in_progress.erase(gid);
    return cache.emplace(gid, std::move(result)).first->second;
  };

  ClassSide out;
  for (auto& [name, body] : msg_body) {
    std::smatch m;
    if (!std::regex_search(body, m, kMsgTypeRe)) throw std::runtime_error("no msg_type_v in class " + name);
    const std::string msg_type = m[1].str();
    if (!out.msg_types.insert(msg_type).second) {
      throw std::runtime_error("duplicate msg_type_v on class side: " + msg_type);
    }
    std::set<int> reach = find_all_ints(body, kMsgTagRe);
    const std::set<int> refs = find_all_ints(body, kGrpRefRe);
    reach.insert(refs.begin(), refs.end());
    for (int r : refs) {
      const std::set<int>& sub = closure(r);
      reach.insert(sub.begin(), sub.end());
    }
    out.reachable[msg_type] = std::move(reach);
  }
  return out;
}

// -------------------------- manifest side ----------------------------

struct ManifestSide {
  std::set<std::string> msg_types;
  std::map<std::string, std::set<int>> reachable;
};

std::vector<std::string> split_tab(const std::string& line) {
  std::vector<std::string> f;
  std::string cur;
  std::istringstream iss(line);
  while (std::getline(iss, cur, '\t')) f.push_back(cur);
  return f;
}

int group_id_of_path(const std::string& path) {
  const auto pos = path.rfind('.');
  const std::string last = (pos == std::string::npos) ? path : path.substr(pos + 1);
  return std::stoi(last);
}

ManifestSide parse_manifest_projected(const std::string& path) {
  const std::vector<std::string> lines = split_lines(read_file(path));

  std::string cur_msg;
  std::map<std::string, std::set<int>> top_level;
  std::map<int, std::set<int>> member_tags;
  std::set<std::string> msg_types;

  for (auto& line : lines) {
    if (line.empty() || line[0] == '#') continue;
    const auto f = split_tab(line);
    if (f.empty()) continue;
    if (f[0] == "MSG") {
      cur_msg = f[1];
      msg_types.insert(cur_msg);
    } else if (f[0] == "OCC") {
      const std::string& gp = f[1];
      const int tag = std::stoi(f[2]);
      if (gp == "-") {
        top_level[cur_msg].insert(tag);
      } else {
        member_tags[group_id_of_path(gp)].insert(tag);
      }
    }
  }

  std::map<int, std::set<int>> cache;
  std::set<int> in_progress;
  std::function<const std::set<int>&(int)> closure = [&](int gid) -> const std::set<int>& {
    auto cached = cache.find(gid);
    if (cached != cache.end()) return cached->second;
    if (in_progress.count(gid)) throw std::runtime_error("manifest-side group cycle at " + std::to_string(gid));
    in_progress.insert(gid);
    std::set<int> result;
    auto mit = member_tags.find(gid);
    if (mit != member_tags.end()) {
      result = mit->second;
      for (int t : mit->second) {
        if (member_tags.count(t)) {
          const std::set<int>& sub = closure(t);
          result.insert(sub.begin(), sub.end());
        }
      }
    }
    in_progress.erase(gid);
    return cache.emplace(gid, std::move(result)).first->second;
  };

  ManifestSide out;
  out.msg_types = std::move(msg_types);
  for (auto& [msg, tags] : top_level) {
    std::set<int> reach = tags;
    for (int t : tags) {
      if (member_tags.count(t)) {
        const std::set<int>& sub = closure(t);
        reach.insert(sub.begin(), sub.end());
      }
    }
    out.reachable[msg] = std::move(reach);
  }
  return out;
}

std::string describe_tags(const std::vector<int>& s, size_t limit = 30) {
  std::ostringstream oss;
  size_t shown = 0;
  for (int t : s) {
    if (shown++ >= limit) { oss << "... (" << (s.size() - shown + 1) << " more)"; break; }
    oss << t << " ";
  }
  return oss.str();
}

struct Sides {
  ClassSide cs;
  ManifestSide ms;
};

Sides build_sides() {
  Sides s;
  s.cs = parse_class_side(FIXPP_CODEGEN_VLATEST_MESSAGES_HPP);
  s.ms = parse_manifest_projected(FIXPP_CODEGEN_VLATEST_MANIFEST);
  return s;
}

}  // namespace

// V-1b leg 1: message-set exact equality (181==181, both directions).
TEST(VlatestManifestClassConsistency, MessageSetExact181) {
  Sides s;
  ASSERT_NO_THROW(s = build_sides());

  EXPECT_EQ(s.cs.msg_types.size(), 181u) << "class-side (Messages.hpp) message count";
  EXPECT_EQ(s.ms.msg_types.size(), 181u) << "manifest-side (projected Manifest.txt) message count";

  std::vector<std::string> only_class, only_manifest;
  std::set_difference(s.cs.msg_types.begin(), s.cs.msg_types.end(), s.ms.msg_types.begin(), s.ms.msg_types.end(),
                       std::back_inserter(only_class));
  std::set_difference(s.ms.msg_types.begin(), s.ms.msg_types.end(), s.cs.msg_types.begin(), s.cs.msg_types.end(),
                       std::back_inserter(only_manifest));

  auto join = [](const std::vector<std::string>& v) {
    std::ostringstream oss;
    for (auto& x : v) oss << x << " ";
    return oss.str();
  };
  EXPECT_TRUE(only_class.empty()) << "msg_types in class side (Messages.hpp) but NOT in manifest: " << join(only_class);
  EXPECT_TRUE(only_manifest.empty()) << "msg_types in manifest but NOT in class side: " << join(only_manifest);
}

// V-1b leg 2 (the class-reachable-field PRIMARY HARD GATE): per message,
// class-reachable(M) == manifest-projected-reachable(M) as an exact set.
TEST(VlatestManifestClassConsistency, PerMessageReachableFieldSetExact) {
  Sides s;
  ASSERT_NO_THROW(s = build_sides());
  ASSERT_EQ(s.cs.msg_types, s.ms.msg_types) << "message sets differ -- see MessageSetExact181";

  std::vector<std::string> mismatches;
  for (const std::string& mt : s.cs.msg_types) {
    const std::set<int>& a = s.cs.reachable.at(mt);
    const std::set<int>& b = s.ms.reachable.at(mt);
    if (a == b) continue;
    std::vector<int> only_class, only_manifest;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(only_class));
    std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(only_manifest));
    std::ostringstream oss;
    oss << "msg_type=" << mt << " class-only=[" << describe_tags(only_class) << "] manifest-only=["
        << describe_tags(only_manifest) << "]";
    mismatches.push_back(oss.str());
  }

  EXPECT_TRUE(mismatches.empty()) << mismatches.size() << " message(s) with a class<->manifest reachable-field mismatch:\n"
                                   << [&] {
                                        std::ostringstream oss;
                                        for (auto& m : mismatches) oss << "  " << m << "\n";
                                        return oss.str();
                                      }();
}
