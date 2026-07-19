// src/dictionary/load_any.cpp
// 080-orchestra-runtime-load — dict::load_any shared root-sniff dispatch
// helper (contracts/load_any.md). Mirrors OrchestraLoader::load's
// ifstream+pugixml open/parse idiom (src/dictionary/orchestra_loader.cpp).

#include <fixpp/dict/load_any.hpp>

#include <cassert>
#include <filesystem>
#include <fixpp/core/decimal_helpers.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <ios>
#include <memory_resource>
#include <pugixml.hpp>
#include <string>
#include <string_view>

namespace fixpp::dict {

Dictionary load_any(std::filesystem::path const& path, std::pmr::memory_resource* mr) {
    assert(mr != nullptr && "load_any: mr must not be null");

    return fixpp::core::detail::trap_throw_or_throw<xml_oom_error>([&] {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw dict::xml_parse_error("dict::xml_parse_error: cannot open " + path.string());
        }
        pugi::xml_document doc;
        auto const result = doc.load(in);
        if (!result) {
            throw dict::xml_parse_error(std::string{"dict::xml_parse_error: "} +
                                         result.description());
        }

        // document_element() = the first ELEMENT child (skips any leading
        // comment/processing-instruction/XML declaration) — N-2 hardening. This
        // aligns with XmlLoader's own comment-skipping doc.child("fix") probe;
        // OrchestraLoader instead re-checks with a raw first_child() (no skip), so
        // an Orchestra file with a leading comment/PI would sniff-accept here yet be
        // rejected on delegation — still fail-closed (a clean dict:: error, never a
        // mis-load), and moot for the shipped comment-free OrchestraFIXLatest.xml.
        auto const root = doc.document_element();
        std::string_view const name{root.name()};

        if (name == "fix") {
            return XmlLoader{}.load(path, mr);
        }
        if (name == "fixr:repository") {
            return OrchestraLoader{}.load(path, mr);
        }
        throw dict::xml_parse_error(std::string{"dict::xml_parse_error: unrecognized dictionary root "
                                                 "element <"} +
                                     std::string{name} + "> (expected <fix> or <fixr:repository>)");
    });
}

}  // namespace fixpp::dict
