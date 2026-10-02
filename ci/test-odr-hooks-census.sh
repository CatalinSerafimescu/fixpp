#!/usr/bin/env bash
# ci/test-odr-hooks-census.sh — pin ci/odr-hooks-census.py (fixpp#530).
#
# No compiler or linker diagnoses a class definition that changes with FIXPP_TEST_HOOKS (an
# ODR violation, #511); the census is what looks. So each verdict and each refusal gets a
# fixture here, and every arm asserts its OWN line of output as well as the exit code: a
# typo also exits non-zero, and a RED reached through a different check than the one an arm
# was written for leaves that check unpinned.
#
# BUILDLESS: each arm is a synthetic tree (include/, src/, tests/), a hand-written
# compile_commands.json and a CMakeCache.txt naming the source dir. Its one dependency is a
# C++ compiler, and it REFUSES to run without one rather than skipping. Override with CXX=...
#
# The real tree is not an arm here: it needs a configured build with generated headers. The
# census runs on it in tier1.yml's linux-clang-release leg.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# ODR_CENSUS_UNDER_TEST is set only by the mutant section below, to run every cell against a
# broken copy.
CENSUS="${ODR_CENSUS_UNDER_TEST:-$HERE/odr-hooks-census.py}"
CXX="${CXX:-c++}"
command -v "$CXX" >/dev/null 2>&1 || { echo "FAIL: no C++ compiler '$CXX' on PATH"; exit 1; }
CXX="$(command -v "$CXX")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0

# put <file> — write stdin to <file>, creating its directory
put() { mkdir -p "$(dirname "$1")"; cat > "$1"; }

# db <tree> <json-entries> — compile_commands.json + CMakeCache.txt under <tree>/build
db() {
  mkdir -p "$1/build"
  printf '[%s]\n' "$2" > "$1/build/compile_commands.json"
  printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\n' "$1" > "$1/build/CMakeCache.txt"
}

# cmd <tree> <file> <extra flags> — one `command`-form entry
cmd() {
  printf '{"directory": "%s/build", "file": "%s/%s", "command": "%s -std=c++17 -I%s/include -I%s/src %s -c %s/%s -o x.o"}' \
    "$1" "$1" "$2" "$CXX" "$1" "$1" "$3" "$1" "$2"
}

# mk <case> — a clean tree; prints its path. Every header is a type the arms seed.
mk() {
  local t="$TMP/$1"
  put "$t/include/fx/widget.hpp" <<'EOF'
#pragma once
namespace fx {
class Widget {
public:
    int a;
};
template <class T = void>
struct Box {
    T* p;
};
struct alignas(8) Aligned {
    int a;
};
enum class Color : int { red };
struct Outer {
    struct Inner {
        int a;
    };
};
}  // namespace fx
EOF
  put "$t/src/impl.hpp" <<'EOF'
#pragma once
#include "fx/widget.hpp"
namespace fx::detail {
struct Impl {
    Widget w;
};
}  // namespace fx::detail
EOF
  put "$t/tests/fixture.hpp" <<'EOF'
#pragma once
namespace fxt {
struct Fixture {
    int n;
};
}  // namespace fxt
EOF
  put "$t/src/a.cpp" <<<'#include "impl.hpp"'
  put "$t/tests/t.cpp" <<<'#include "fixture.hpp"'
  db "$t" "$(cmd "$t" src/a.cpp ''), $(cmd "$t" tests/t.cpp '-DFIXPP_TEST_HOOKS')"
  echo "$t"
}

# check <name> <want-rc> <tree> <want>... — a <want> is a substring of some output line, or,
# when it starts with '^', a WHOLE line (leading blanks ignored): a class name is a substring
# of its nested classes' names, so a substring cannot tell them apart.
check() {
  local name="$1" want_rc="$2" tree="$3" out rc w; shift 3
  out="$(python3 "$CENSUS" --build-dir "$tree/build" -j 2 2>&1)"; rc=$?
  if [ "$rc" != "$want_rc" ]; then
    echo "FAIL  $name: exit $rc, wanted $want_rc"; echo "$out" | sed 's/^/      /' | head -12
    fail=$((fail+1)); return
  fi
  for w in "$@"; do
    if [ "${w:0:1}" = "^" ]; then
      printf '%s\n' "$out" | sed 's/^ *//' | grep -qxF -- "${w:1}"
    else
      printf '%s\n' "$out" | grep -qF -- "$w"
    fi
    if [ $? != 0 ]; then
      echo "FAIL  $name: exit $rc as wanted, but no line containing: $w"
      echo "$out" | sed 's/^/      /' | head -12; fail=$((fail+1)); return
    fi
  done
  echo "ok    $name"; pass=$((pass+1))
}

NODIV="(ODR): 0"
NOONE="(one definition, not a divergence): 0"

# ── GREEN controls: without these every RED below could be an always-RED checker ─────────────
check "T0 a clean tree passes, with the positive control run" 0 "$(mk t0)" \
  "positive control: ok" "$NODIV" "$NOONE" "include/=1 src/=1 tests/=1"

# ── a gated member in a class under each root ────────────────────────────────────────────────
t="$(mk t1)"; sed -i 's/^    int a;$/    int a;\n#ifdef FIXPP_TEST_HOOKS\n    int seeded;\n#endif/' "$t/include/fx/widget.hpp"
check "T1 a gated member of a class under include/ is a DIVERGENCE" 1 "$t" \
  "include/fx/widget.hpp: fx::Widget" "+ int seeded;"

t="$(mk t2)"; sed -i 's/^    Widget w;$/    Widget w;\n#ifdef FIXPP_TEST_HOOKS\n    void seeded() {}\n#endif/' "$t/src/impl.hpp"
check "T2 a gated member of a class under src/ is a DIVERGENCE" 1 "$t" \
  "src/impl.hpp: fx::detail::Impl"

t="$(mk t3)"; sed -i 's/^    int n;$/    int n;\n#if defined(FIXPP_TEST_HOOKS)\n    int seeded;\n#endif/' "$t/tests/fixture.hpp"
check "T3 a gated member of a class under tests/ is a DIVERGENCE" 1 "$t" \
  "tests/fixture.hpp: fxt::Fixture"

t="$(mk t4)"; sed -i 's/^public:$/#ifdef FIXPP_TEST_HOOKS\n    friend struct widget_peek;\n#endif\npublic:/' "$t/include/fx/widget.hpp"
check "T4 a gated FRIEND is a DIVERGENCE" 1 "$t" "fx::Widget" "+ friend struct widget_peek;"

# ── heads the scope classifier must still recognise as a class ───────────────────────────────
t="$(mk t5)"; sed -i 's/^    T\* p;$/    T* p;\n#ifdef FIXPP_TEST_HOOKS\n    int seeded;\n#endif/' "$t/include/fx/widget.hpp"
check "T5 a class template with a DEFAULT template argument (a head holding '=')" 1 "$t" "fx::Box"

t="$(mk t6)"; awk '/struct alignas/{f=1} f&&/int a;/{print; print "#ifdef FIXPP_TEST_HOOKS"; print "    int seeded;"; print "#endif"; f=0; next} {print}' \
  "$t/include/fx/widget.hpp" > "$t/w" && mv "$t/w" "$t/include/fx/widget.hpp"
check "T6 a class head holding '(' (alignas)" 1 "$t" "fx::Aligned"

t="$(mk t7)"; sed -i 's/{ red };/{ red,\n#ifdef FIXPP_TEST_HOOKS\n    seeded,\n#endif\n};/' "$t/include/fx/widget.hpp"
check "T7 a gated enumerator (an enumeration is a type definition too)" 1 "$t" "fx::Color"

t="$(mk t8)"; sed -i 's/^class Widget {$/struct Hook {};\nclass Widget\n#ifdef FIXPP_TEST_HOOKS\n    : public Hook\n#endif\n{/' "$t/include/fx/widget.hpp"
check "T8 a gated BASE CLAUSE (the head differs, the body does not)" 1 "$t" "fx::Widget"

t="$(mk t9)"; awk '/struct Inner/{f=1} f&&/int a;/{print; print "#ifdef FIXPP_TEST_HOOKS"; print "        int seeded;"; print "#endif"; f=0; next} {print}' \
  "$t/include/fx/widget.hpp" > "$t/w" && mv "$t/w" "$t/include/fx/widget.hpp"
check "T9 a gated member of a NESTED class reports it and its enclosing class" 1 "$t" \
  "^include/fx/widget.hpp: fx::Outer::Inner" "^include/fx/widget.hpp: fx::Outer"

# A brace inside a raw string literal that spans lines is text, not scope: read line by line,
# the `}` would close Widget before the gated member, which would then be namespace scope.
t="$(mk t23)"; sed -i 's/^    int a;$/    int a;\n    static constexpr const char* text = R"x(\n}\n)x";\n#ifdef FIXPP_TEST_HOOKS\n    int seeded;\n#endif/' "$t/include/fx/widget.hpp"
check "T23 a raw string spanning lines does not end the class early" 1 "$t" "^include/fx/widget.hpp: fx::Widget"

# ── what is NOT a divergence ─────────────────────────────────────────────────────────────────
t="$(mk t10)"; sed -i 's/^enum class/#ifdef FIXPP_TEST_HOOKS\nvoid seeded_ns_decl() noexcept;\n#endif\nenum class/' "$t/include/fx/widget.hpp"
check "T10 a gated NAMESPACE-scope declaration passes, and is listed" 0 "$t" \
  "$NODIV" "+ void seeded_ns_decl() noexcept;"

t="$(mk t11)"; cat >> "$t/tests/fixture.hpp" <<'EOF'
#ifdef FIXPP_TEST_HOOKS
namespace fxt {
struct only_hooked {
    int a;
};
}  // namespace fxt
#endif
EOF
check "T11 a struct that exists ONLY under the macro is one definition, not a divergence" 0 "$t" \
  "$NODIV" "tests/fixture.hpp: fxt::only_hooked (only with FIXPP_TEST_HOOKS)"

# ...but the same name defined differently in each state is two definitions.
t="$(mk t12)"; cat >> "$t/tests/fixture.hpp" <<'EOF'
namespace fxt {
#ifdef FIXPP_TEST_HOOKS
struct split { int hooked; };
#else
struct split { int plain; };
#endif
}  // namespace fxt
EOF
check "T12 #ifdef/#else definitions of ONE class are a DIVERGENCE" 1 "$t" "tests/fixture.hpp: fxt::split"

# L-530-1(a), pinned as the documented behaviour: a gated statement in a function body outside
# any class is listed, and does NOT fail. If that ever changes, this cell and the row change too.
t="$(mk t24)"; cat >> "$t/tests/fixture.hpp" <<'EOF'
namespace fxt {
inline int counter() {
    int n = 0;
#ifdef FIXPP_TEST_HOOKS
    n += 1;
#endif
    return n;
}
}  // namespace fxt
EOF
check "T24 a gated statement in a namespace-scope inline function is listed, not failed (L-530-1)" 0 "$t" \
  "$NODIV" "$NOONE" "+ n += 1;"

# ── how the macro reaches the database: every spelling is stripped from the base flags ───────
# If one leaked into the shared define set, both runs would define the macro and every header
# would compare equal. The positive control then refuses (2), so a 1 here proves the strip.
for sp in "-DFIXPP_TEST_HOOKS" "-DFIXPP_TEST_HOOKS=1" "-D FIXPP_TEST_HOOKS"; do
  t="$(mk "t13${sp// /_}")"
  sed -i 's/^    int a;$/    int a;\n#ifdef FIXPP_TEST_HOOKS\n    int seeded;\n#endif/' "$t/include/fx/widget.hpp"
  db "$t" "$(cmd "$t" src/a.cpp "$sp"), $(cmd "$t" tests/t.cpp "$sp")"
  check "T13 every entry carries '$sp': the seeded member is still a DIVERGENCE" 1 "$t" \
    "positive control: ok" "fx::Widget"
done

t="$(mk t14)"
sed -i 's/^    int a;$/    int a;\n#ifdef FIXPP_TEST_HOOKS\n    int seeded;\n#endif/' "$t/include/fx/widget.hpp"
db "$t" "$(printf '{"directory": "%s/build", "file": "%s/src/a.cpp", "arguments": ["ccache", "%s", "-std=c++17", "-I%s/include", "-I", "%s/src", "-c", "%s/src/a.cpp", "-o", "a.o", "@a.cpp.o.modmap"]}' \
  "$t" "$t" "$CXX" "$t" "$t" "$t"), $(cmd "$t" tests/t.cpp '')"
check "T14 the 'arguments' form, a launcher, a split -I and a module map are read" 1 "$t" \
  "fx::Widget" "compiler: $CXX;"

# ── refusals (2): each is a way the scan could otherwise report clean ────────────────────────
t="$(mk t15)"; printf '#ifndef FIXPP_TEST_HOOKS\n#error needs the macro\n#endif\n' >> "$t/tests/fixture.hpp"
check "T15 a header that fails to preprocess in ONE state is an ERROR" 2 "$t" \
  "ERROR tests/fixture.hpp: fails without FIXPP_TEST_HOOKS"

t="$(mk t16)"; printf '#include "fx/no_such_header.hpp"\n' >> "$t/src/impl.hpp"
check "T16 a missing include is an ERROR, and the findings are still printed" 2 "$t" \
  "ERROR src/impl.hpp: fails without and with FIXPP_TEST_HOOKS" "$NODIV"

t="$(mk t17)"; rm -f "$t"/include/fx/*.hpp "$t"/src/*.hpp "$t"/tests/*.hpp
check "T17 an EMPTY header population is refused" 2 "$t" "include/ contributes no header" \
  "src/ contributes no header" "tests/ contributes no header"

t="$(mk t18)"; rm -f "$t"/tests/*.hpp
check "T18 one root contributing NO header is refused" 2 "$t" "tests/ contributes no header"

t="$(mk t19)"; db "$t" '{"directory": "/", "file": "/x.c", "command": "cc -c /x.c"}'
check "T19 a database with no C++ entry is refused" 2 "$t" "has no C++ entry"

t="$(mk t20)"; o="$(mk t20other)"; db "$t" "$(cmd "$o" src/a.cpp '')"
printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\n' "$t" > "$t/build/CMakeCache.txt"
check "T20 a database from ANOTHER tree is refused (nothing is re-rooted)" 2 "$t" \
  "the database belongs to another tree"

t="$(mk t21)"; printf '#define FIXPP_TEST_HOOKS 1\n' > "$t/force.h"
db "$t" "$(cmd "$t" src/a.cpp "-include $t/force.h"), $(cmd "$t" tests/t.cpp "-include $t/force.h")"
check "T21 the macro defined in BOTH runs (a forced include) fails the positive control" 2 "$t" \
  "REFUSED: positive control" "positive control: FAILED"

t="$(mk t22)"; db "$t" "$(cmd "$t" src/a.cpp '@flags.rsp')"
check "T22 a response file other than a module map is refused (its flags are unreadable)" 2 "$t" \
  "response file @flags.rsp"

# ── mutants: each cell above must be able to fail ──────────────────────────────────────────────
# A cell that cannot fail when the rule it names is broken measures its own setup. Each mutant
# is a copy of the census with one rule broken; the whole suite runs against it, and the cell
# that names that rule must be among the failures. A mutant whose anchor text is no longer in
# the census fails too, so a refactor cannot retire a mutant silently.
if [ -z "${ODR_CENSUS_UNDER_TEST:-}" ]; then
  # mutant <cell> <description> <python: old> <python: new>
  mutant() {
    local cell="$1" what="$2" m="$TMP/mutant.py" out
    if ! python3 - "$HERE/odr-hooks-census.py" "$m" "$3" "$4" <<'PY'
import sys
src, dst, old, new = sys.argv[1:]
s = open(src).read()
if s.count(old) != 1:
    sys.exit(1)
open(dst, "w").write(s.replace(old, new))
PY
    then
      echo "FAIL  mutant '$what': its anchor is not in the census exactly once"; fail=$((fail+1)); return
    fi
    out="$(ODR_CENSUS_UNDER_TEST="$m" bash "${BASH_SOURCE[0]}" 2>&1)"
    if printf '%s\n' "$out" | grep -q "^FAIL  $cell "; then
      echo "ok    mutant '$what' reddens $cell"; pass=$((pass+1))
    else
      echo "FAIL  mutant '$what' left $cell green"; fail=$((fail+1))
    fi
  }
  mutant T5 "the old rule: a head holding '=' or '(' is not a class" \
    '    """-> ('"'"'namespace'"'"'|'"'"'type'"'"'|'"'"'other'"'"', name) for the tokens before a `{`."""' \
    '    """-> ('"'"'namespace'"'"'|'"'"'type'"'"'|'"'"'other'"'"', name) for the tokens before a `{`."""
    if "=" in h or "(" in h:
        return "other", ""'
  mutant T6 "the old rule, again: alignas(...) hides the class" \
    '    """-> ('"'"'namespace'"'"'|'"'"'type'"'"'|'"'"'other'"'"', name) for the tokens before a `{`."""' \
    '    """-> ('"'"'namespace'"'"'|'"'"'type'"'"'|'"'"'other'"'"', name) for the tokens before a `{`."""
    if "(" in h:
        return "other", ""'
  mutant T13 "a spelling of the macro is not stripped from the base flags" \
    '    return name_and_value.split("=", 1)[0] == MACRO' \
    '    return name_and_value == MACRO'
  mutant T11 "a definition in ONE state only is reported as a divergence" \
    '        else:
            side = "with" if k in db else "without"' \
    '        else:
            div[k] = ((db if k in db else da)[k][2], [])
            side = "with" if k in db else "without"'
  mutant T8 "only the BODY is compared, not the head" \
    '                toks_by_frame.append(list(head) + ["{"])' \
    '                toks_by_frame.append(["{"])'
  mutant T9 "a nested definition is not part of its enclosing class" \
    '        for acc in toks_by_frame:' \
    '        for acc in toks_by_frame[-1:]:'
  mutant T23 "a raw string literal cannot span lines" \
    '(?:u8|u|U|L)?R"([^(\s]*)\((?:.|\n)*?\)\1"' \
    '(?:u8|u|U|L)?R"([^(\s]*)\(.*?\)\1"'
  mutant T15 "a preprocessing error does not fail the run" \
    '    if refusals or errors:' \
    '    if refusals:'
  mutant T18 "an empty root is not refused" \
    '        if n == 0:' \
    '        if False:'
  mutant T21 "the positive control is not checked" \
    '    if r["status"] != "DIFF" or list(r["div"]) != [PROBE_KEY]:' \
    '    if False:'
fi

echo
echo "test-odr-hooks-census: $pass passed, $fail failed (CXX=$CXX)"
[ "$fail" = 0 ] && [ "$pass" -gt 0 ]
