// src/dictionary/load_any.cpp
// 080-orchestra-runtime-load — dict::load_any shared root-sniff dispatch
// helper (contracts/load_any.md). Mirrors OrchestraLoader::load's
// ifstream+pugixml open/parse idiom (src/dictionary/orchestra_loader.cpp).

#include <fixpp/dict/load_any.hpp>

#include <cassert>
#include <filesystem>
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

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw dict::xml_parse_error("dict::xml_parse_error: cannot open " + path.string());
    }
    pugi::xml_document doc;
    auto const result = doc.load(in);
    if (!result) {
        throw dict::xml_parse_error(std::string{"dict::xml_parse_error: "} + result.description());
    }

    // document_element() = the first ELEMENT child (skips any leading
    // comment/processing-instruction/XML declaration) — N-2 hardening, keeps
    // this discriminant aligned with XmlLoader's own doc.child("fix") probe.
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
}

}  // namespace fixpp::dict
