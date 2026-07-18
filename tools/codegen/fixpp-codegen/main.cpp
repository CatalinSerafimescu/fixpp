// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/main.cpp — T015. CLI driver.
//
// Usage: fixpp-codegen --xml <FIXxx.xml> --out <build-tree-dir> [--xml ... --out ...]
//
// Per --xml/--out pair: build_ir(xml) -> run the per-version emitters -> write
// <out>/<vXX>/{Messages,Fields,Validator,Reify,NormativeReferences}.{hpp,md}.
// After all versions, emit the shared _dispatch/ headers once over the union.
// Output goes ONLY where --out points; CMake passes the build tree
// (build/<preset>/_codegen/include/fixpp) so nothing is ever written to the
// source tree (AC-C4/AC-T2). Build-only host tool ([const §III.5]); exceptions
// are allowed here ([arch §5.3]) -- XmlLoader::load throws on bad XML.
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory_resource>
#include <string_view>
#include <utility>
#include <vector>

#include "emit.hpp"
#include "ir.hpp"

namespace {

void write_file(std::filesystem::path const& p, std::string_view content) {
    if (content.empty()) {
        return;  // emitter not yet active for this artifact
    }
    std::filesystem::create_directories(p.parent_path());
    std::ofstream os(p, std::ios::binary | std::ios::trunc);
    os.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct Job {
    std::filesystem::path xml;
    std::filesystem::path out;
};

// 078-precompiled-builder-libs (data-model.md Entity 7 / quickstart V3) --
// OFF-clean discipline for the split builder/validator file SET: `write_file`
// only writes, it never deletes, so every generation run must sweep the
// whole builder/validator output region -- Builders.hpp/groups.hpp/all.hpp
// plus the groups/, validators/, messages/ dirs -- before (re)writing it.
// Covers a version that emits no builders this run (vt11's empty registry;
// v42 excluded below), a legacy pre-078 monolith, and a prior run's stale
// per-plan groups/*.hpp.
void remove_builder_output(std::filesystem::path const& base) {
    std::error_code ec;
    std::filesystem::remove(base / "Builders.hpp", ec);
    std::filesystem::remove(base / "groups.hpp", ec);
    std::filesystem::remove(base / "all.hpp", ec);
    std::filesystem::remove_all(base / "groups", ec);
    std::filesystem::remove_all(base / "validators", ec);
    std::filesystem::remove_all(base / "messages", ec);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<Job> jobs;
    std::filesystem::path cur_xml;
    std::filesystem::path cur_out;
    // 069-v44-all-families T007 (data-model.md Entity "Coverage mode"):
    // default `all` — the 83-in-scope write surface is on by default; opt
    // down to `official` (the frozen 33) via --families.
    fixpp::codegen::CoverageMode families_mode = fixpp::codegen::CoverageMode::All;
    for (int i = 1; i < argc; ++i) {
        std::string_view const a = argv[i];
        if (a == "--xml" && i + 1 < argc) {
            cur_xml = argv[++i];
        } else if (a == "--out" && i + 1 < argc) {
            cur_out = argv[++i];
        } else if (a == "--families" && i + 1 < argc) {
            std::string_view const v = argv[++i];
            if (v == "all") {
                families_mode = fixpp::codegen::CoverageMode::All;
            } else if (v == "official") {
                families_mode = fixpp::codegen::CoverageMode::Official;
            } else {
                std::cerr << "fixpp-codegen: --families must be 'all' or 'official', got '" << v
                          << "'\n";
                return 2;
            }
        }
        if (!cur_xml.empty() && !cur_out.empty()) {
            jobs.push_back({.xml = cur_xml, .out = cur_out});
            cur_xml.clear();
            cur_out.clear();
        }
    }
    if (jobs.empty()) {
        std::cerr << "fixpp-codegen: need at least one --xml <path> --out <dir> pair\n";
        return 2;
    }

    try {
        std::pmr::monotonic_buffer_resource arena;
        std::vector<fixpp::codegen::VersionIR> all;
        for (auto const& j : jobs) {
            fixpp::codegen::VersionIR ir = fixpp::codegen::build_ir(j.xml, &arena);
            std::filesystem::path const base = j.out / ir.ns;
            write_file(base / "Fields.hpp", fixpp::codegen::emit_fields(ir));
            write_file(base / "Messages.hpp", fixpp::codegen::emit_messages(ir));
            write_file(base / "Validator.hpp", fixpp::codegen::emit_validator(ir));
            write_file(base / "Reify.hpp", fixpp::codegen::emit_reify(ir));
            write_file(base / "NormativeReferences.md", fixpp::codegen::emit_normative_refs(ir));
            write_file(base / "Manifest.txt", fixpp::codegen::emit_manifest(ir));
            // 077-builder-args-dedup T017 (issue #196 / L-063-1): v42 types
            // NumInGroup as legacy INT (not NUMINGROUP), so emit_builders
            // materializes ZERO typed groups for it -- a v42 build_<Msg>
            // would silently omit a required='Y' group, emitting invalid
            // FIX 4.2. v42 still has in-scope application messages (just no
            // typed groups), so emit_builders(v42) does NOT self-skip via an
            // empty file set -- it must be excluded at the DRIVER, not
            // inside emit_builders (which stays version-agnostic per
            // FR-005/T009). vt11 is the only version that self-skips via a
            // truly empty registry (0 application messages,
            // emit_builders.cpp).
            //
            // 078-precompiled-builder-libs T004: emit_builders now returns
            // the split file SET (data-model.md Entities 1-5) instead of a
            // single Builders.hpp string. v42 stays descoped (issue #196 /
            // L-063-1) -- keep the existing ir.ns != "v42" predicate, it
            // just yields an empty write set here instead of a separate
            // branch. remove_builder_output sweeps the whole builder/
            // validator output region unconditionally before writing, so
            // every branch (empty set / v42 / non-empty) is OFF-clean.
            std::vector<fixpp::codegen::EmittedFile> files;
            if (ir.ns != "v42") {
                files = fixpp::codegen::emit_builders(ir, families_mode);
            }
            remove_builder_output(base);
            for (auto const& f : files) {
                write_file(base / f.rel, f.content);
            }
            all.push_back(std::move(ir));
        }
        // Shared dispatch headers -- emitted once over the union ([2c §4.8]).
        std::filesystem::path const disp = jobs.front().out / "_dispatch";
        write_file(disp / "reify_dispatch_fixt.hpp", fixpp::codegen::emit_dispatch_fixt(all));
        write_file(disp / "reify_dispatch_application.hpp",
                   fixpp::codegen::emit_dispatch_application(all));
    } catch (std::exception const& e) {
        std::cerr << "fixpp-codegen: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
