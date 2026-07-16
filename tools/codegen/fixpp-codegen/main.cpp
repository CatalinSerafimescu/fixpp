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
            write_file(base / "Builders.hpp", fixpp::codegen::emit_builders(ir, families_mode));
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
