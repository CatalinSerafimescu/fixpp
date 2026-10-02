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
# ODR_CENSUS_UNDER_TEST is set by the mutant section below, to run a cell against a broken copy
# (ODR_CELL names the cell); set by hand, it runs every cell against another census.
CENSUS="${ODR_CENSUS_UNDER_TEST:-$HERE/odr-hooks-census.py}"
# ODR_CELL selects one cell, for a mutant's run only. Inherited by a run with no census under
# test, it would skip every other cell of a run that still reports success, so it is refused.
if [ -n "${ODR_CELL:-}" ] && [ -z "${ODR_CENSUS_UNDER_TEST:-}" ]; then
  echo "FAIL: ODR_CELL=$ODR_CELL is set without ODR_CENSUS_UNDER_TEST; it would skip every other cell"
  exit 1
fi
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
  # ODR_CELL is set only by mutant() below. A mutant's evidence is the FAIL line of the one cell
  # it names, so every other cell is skipped, and counted neither way.
  [ -n "${ODR_CELL:-}" ] && [ "${name%% *}" != "$ODR_CELL" ] && return
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

# wellformed <name> <std> <file> — compile <file> in both macro states. The census only
# preprocesses, so a fixture no compiler accepts would pin a shape no real header can have.
# The fixtures are the same bytes in every mutant run, so only the top-level run compiles.
wellformed() {
  local name="$1" std="$2" f="$3" d
  [ -n "${ODR_CENSUS_UNDER_TEST:-}" ] && return 0
  for d in "" "-DFIXPP_TEST_HOOKS"; do
    if ! "$CXX" "$std" -fsyntax-only -x c++ $d "$f" >"$TMP/wellformed.err" 2>&1; then
      echo "FAIL  $name: its fixture does not compile${d:+ with $d}"
      sed 's/^/      /' "$TMP/wellformed.err" | head -6; fail=$((fail+1)); return 1
    fi
  done
}

# fixture <name> <rel> <std> <want>... — a clean tree plus <rel> (read from stdin), compiled
# in both states, then required to be a DIVERGENCE naming each <want>.
fixture() {
  local name="$1" rel="$2" std="$3" t; shift 3
  t="$(mk "fx${name%% *}")"; put "$t/$rel"
  wellformed "$name" "$std" "$t/$rel" && check "$name" 1 "$t" "$@"
}

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
# the `}` would close Widget before the gated member.
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

# L-530-1(b), pinned the same way: every other macro is held in ONE state, defined if any entry
# defines it. A member gated on the macro and on another macro's ABSENCE therefore never shows,
# though the TUs of the entry that lacks that macro do see it change.
t="$(mk t44)"; cat >> "$t/tests/fixture.hpp" <<'EOF'
namespace fxt {
struct seam_dependent {
    int a;
#if defined(FIXPP_TEST_HOOKS) && !defined(FIXPP_ASYNC_MUTEX_TEST_SEAM)
    int gated;
#endif
};
}  // namespace fxt
EOF
db "$t" "$(cmd "$t" src/a.cpp ''), $(cmd "$t" tests/t.cpp '-DFIXPP_ASYNC_MUTEX_TEST_SEAM')"
check "T44 a macro some entries lack is held DEFINED, so its absent state is not seen (L-530-1)" 0 "$t" \
  "positive control: ok" "$NODIV"

# ...and a define the entries give different values is held at the first entry's value in PATH
# order, not database order: here the database lists tests/t.cpp (=1) before src/a.cpp (=2).
t="$(mk t45)"; cat >> "$t/tests/fixture.hpp" <<'EOF'
namespace fxt {
struct level_dependent {
    int a;
#if defined(FIXPP_TEST_HOOKS) && FX_LEVEL == 1
    int gated;
#endif
};
}  // namespace fxt
EOF
db "$t" "$(cmd "$t" tests/t.cpp '-DFX_LEVEL=1'), $(cmd "$t" src/a.cpp '-DFX_LEVEL=2')"
check "T45 a multi-valued define is held at its first value in path order (L-530-1)" 0 "$t" \
  "positive control: ok" "$NODIV"

# ── the population: every suffix in HEADER_EXT is scanned ───────────────────────────────────
fixture "T25 a gated member of a class in a .h header is a DIVERGENCE" include/fx/capi.h -std=c++17 \
  "^include/fx/capi.h: fx_capi" <<'EOF'
#pragma once
struct fx_capi {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
EOF
fixture "T26 a gated member of a class in an .inl fragment is a DIVERGENCE" include/fx/frag.inl -std=c++17 \
  "^include/fx/frag.inl: fx::Frag" <<'EOF'
#pragma once
namespace fx {
struct Frag {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T27 a gated member of a class in an .ipp fragment is a DIVERGENCE" src/frag.ipp -std=c++17 \
  "^src/frag.ipp: fx::Frag" <<'EOF'
#pragma once
namespace fx {
struct Frag {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF

# ── heads whose class key follows other tokens ───────────────────────────────────────────────
# One rule per cell. A cell's record sits at namespace scope unless its rule needs an
# enclosing class; then it names the nested class by its whole line, because the enclosing
# class diverges whether or not the nested head is recognised.
fixture "T28 a final class" include/fx/h_final.hpp -std=c++17 "^include/fx/h_final.hpp: fx::Sealed" <<'EOF'
namespace fx {
class Sealed final {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
};
}  // namespace fx
EOF
fixture "T29 a class right after an access label" include/fx/h_access.hpp -std=c++17 \
  "^include/fx/h_access.hpp: fx::Holder::In" <<'EOF'
namespace fx {
struct Holder {
private:
    struct In {
        int a = 0;
#ifdef FIXPP_TEST_HOOKS
        int seeded = 0;
#endif
    };
};
}  // namespace fx
EOF
fixture "T30 a typedef struct" include/fx/h_typedef.hpp -std=c++17 "^include/fx/h_typedef.hpp: fx::td_tag" <<'EOF'
namespace fx {
typedef struct td_tag {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
} td_t;
}  // namespace fx
EOF
fixture "T31 a static struct" include/fx/h_static.hpp -std=c++17 "^include/fx/h_static.hpp: fx::St" <<'EOF'
namespace fx {
static struct St {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
} st;
}  // namespace fx
EOF
fixture "T32 a constexpr struct" include/fx/h_constexpr.hpp -std=c++17 "^include/fx/h_constexpr.hpp: fx::Ce" <<'EOF'
namespace fx {
constexpr struct Ce {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
} ce{};
}  // namespace fx
EOF
fixture "T33 a constinit struct" include/fx/h_constinit.hpp -std=c++20 "^include/fx/h_constinit.hpp: fx::Ci" <<'EOF'
namespace fx {
constinit struct Ci {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
} ci{};
}  // namespace fx
EOF
fixture "T34 a const struct" include/fx/h_const.hpp -std=c++17 "^include/fx/h_const.hpp: fx::Co" <<'EOF'
namespace fx {
const struct Co {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
} co{};
}  // namespace fx
EOF
fixture "T35 a volatile struct" include/fx/h_volatile.hpp -std=c++17 "^include/fx/h_volatile.hpp: fx::Vo" <<'EOF'
namespace fx {
volatile struct Vo {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
} vo;
}  // namespace fx
EOF
fixture "T36 an inline constexpr struct (a function object)" include/fx/h_inline.hpp -std=c++17 \
  "^include/fx/h_inline.hpp: fx::fn_t" <<'EOF'
namespace fx {
inline constexpr struct fn_t {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
} fn{};
}  // namespace fx
EOF
fixture "T37 a thread_local struct" include/fx/h_thread_local.hpp -std=c++17 \
  "^include/fx/h_thread_local.hpp: fx::Tl" <<'EOF'
namespace fx {
thread_local struct Tl {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
} tl;
}  // namespace fx
EOF
fixture "T38 a mutable struct member" include/fx/h_mutable.hpp -std=c++17 \
  "^include/fx/h_mutable.hpp: fx::MHolder::Mu" <<'EOF'
namespace fx {
struct MHolder {
    mutable struct Mu {
        int a = 0;
#ifdef FIXPP_TEST_HOOKS
        int seeded = 0;
#endif
    } mu;
};
}  // namespace fx
EOF
fixture "T39 an extern struct" include/fx/h_extern.hpp -std=c++17 "^include/fx/h_extern.hpp: fx::Ex" <<'EOF'
namespace fx {
extern struct Ex {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
} ex;
}  // namespace fx
EOF
fixture "T40 an extern \"C\" struct (a linkage string before one declaration)" include/fx/h_extern_c.hpp \
  -std=c++17 "^include/fx/h_extern_c.hpp: fx::Lk" <<'EOF'
namespace fx {
extern "C" struct Lk {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
} lk;
}  // namespace fx
EOF
fixture "T41 a struct defined in an alias declaration" include/fx/h_using.hpp -std=c++17 \
  "^include/fx/h_using.hpp: fx::Z" <<'EOF'
namespace fx {
using AliasZ = struct Z {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T42 attributes before the specifiers" include/fx/h_attr.hpp -std=c++17 "^include/fx/h_attr.hpp: fx::At" <<'EOF'
namespace fx {
[[maybe_unused]] static struct At {
    int a = 0;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
} at;
}  // namespace fx
EOF
fixture "T43 an __extension__ struct" include/fx/h_extension.hpp -std=c++17 \
  "^include/fx/h_extension.hpp: fx::Ext" <<'EOF'
namespace fx {
__extension__ struct Ext {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF

# ── a brace inside a class head is an expression, not the body ──────────────────────────────
# A `{` met while a `(` or `[` of the head is open, or a template `<` of the head, or where a
# requires-expression's body goes, belongs to the head. Read as a scope, it would close a fake
# definition early and leave the real body outside any type definition.
fixture "T46a a lambda inside an open paren of a class head (alignas)" include/fx/h_paren_lambda.hpp -std=c++20 \
  "^include/fx/h_paren_lambda.hpp: fx::PL" <<'EOF'
namespace fx {
struct alignas(sizeof(decltype([] {})) * 8) PL {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T46b a lambda inside a base clause's template argument" include/fx/h_base_lambda.hpp -std=c++20 \
  "^include/fx/h_base_lambda.hpp: fx::BL" <<'EOF'
namespace fx {
template <class> struct LB {};
struct BL : LB<decltype([] {})> {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T47 a braced default argument in a template head" include/fx/h_tmpl_brace.hpp -std=c++17 \
  "^include/fx/h_tmpl_brace.hpp: fx::TB" <<'EOF'
namespace fx {
template <int N = int{3}>
struct TB {
    int a = N;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
};
}  // namespace fx
EOF
fixture "T48 a braced template argument in a base clause, no paren open" include/fx/h_base_brace.hpp -std=c++20 \
  "^include/fx/h_base_brace.hpp: fx::BB" <<'EOF'
namespace fx {
struct NP { int v; };
template <NP> struct NB {};
struct BB : NB<NP{1}> {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T49 a requires-expression before the class key" include/fx/h_requires.hpp -std=c++20 \
  "^include/fx/h_requires.hpp: fx::RQ" <<'EOF'
namespace fx {
template <class T>
requires requires {
    typename T::base;
#ifdef FIXPP_TEST_HOOKS
    typename T::hook;
#endif
}
struct RQ {
    int a;
};
}  // namespace fx
EOF
fixture "T62 a braced template argument in a class name's specialization arguments" include/fx/h_spec_brace.hpp \
  -std=c++20 "^include/fx/h_spec_brace.hpp: fx::NS<NP{1}>" <<'EOF'
namespace fx {
struct NP { int v; };
template <NP> struct NS;
template <>
struct NS<NP{1}> {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T64 a member class template of a constrained class template, defined out of class" \
  include/fx/h_two_heads.hpp -std=c++20 "^include/fx/h_two_heads.hpp: fx::CT<T>::In" <<'EOF'
namespace fx {
template <class T>
requires (sizeof(T) > 0)
struct CT {
    template <bool B> struct In;
};
template <class T>
requires (sizeof(T) > 0)
template <bool B>
struct CT<T>::In {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T65 a nested requirement is not read as a class head" include/fx/h_nested_req.hpp -std=c++20 \
  "^include/fx/h_nested_req.hpp: fx::NY" <<'EOF'
namespace fx {
template <class T>
concept NC = requires {
    requires !requires { typename T::absent; };
};
struct NY {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
# Controls: the head rules must not swallow a function body and then misread the next head.
fixture "T50 a struct after a function with a trailing requires-clause" include/fx/h_trailing.hpp -std=c++20 \
  "^include/fx/h_trailing.hpp: fx::TY" <<'EOF'
namespace fx {
template <class T>
void tr() requires (sizeof(T) > 0) {}
struct TY {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T51 a struct after an operator< definition" include/fx/h_oplt.hpp -std=c++17 \
  "^include/fx/h_oplt.hpp: fx::OY" <<'EOF'
namespace fx {
struct OA {};
inline bool operator<(OA, OA) { return false; }
struct OY {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF

# ── a class head the rules cannot read is refused (2), never read as clean ──────────────────
# A `<` after a name opens a template argument list. After a name that is not a template it is
# a comparison, and the census cannot tell the two apart; the list it counted then does not
# close, and that is refused. Each refusal has a parenthesised twin that must be read (1).
# fresh <name> <rel> <std> <want-rc> <want>... — fixture() with any wanted exit code
fresh() {
  local name="$1" rel="$2" std="$3" rc="$4" t; shift 4
  t="$(mk "fx${name%% *}")"; put "$t/$rel"
  wellformed "$name" "$std" "$t/$rel" && check "$name" "$rc" "$t" "$@"
}
fresh "T56 a comparison in a template head is refused" include/fx/h_tmpl_lt.hpp -std=c++17 2 \
  "ERROR include/fx/h_tmpl_lt.hpp: a template argument list in a class head does not close" <<'EOF'
namespace fx {
constexpr int M = 2;
template <int N = M < 3>
struct TL {
    int a = N;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
};
}  // namespace fx
EOF
fixture "T57 the same comparison in parentheses is read" include/fx/h_tmpl_lt_paren.hpp -std=c++17 \
  "^include/fx/h_tmpl_lt_paren.hpp: fx::TP" <<'EOF'
namespace fx {
constexpr int M = 2;
template <int N = (M < 3)>
struct TP {
    int a = N;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
};
}  // namespace fx
EOF
fresh "T58 a comparison in a base clause's template argument is refused" include/fx/h_base_lt.hpp -std=c++17 2 \
  "ERROR include/fx/h_base_lt.hpp: a template argument list in a class head does not close" <<'EOF'
namespace fx {
constexpr int M = 2;
template <bool> struct LT {};
struct BT : LT<M < 3> {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
fixture "T59 the same base-clause comparison in parentheses is read" include/fx/h_base_lt_paren.hpp -std=c++17 \
  "^include/fx/h_base_lt_paren.hpp: fx::BP" <<'EOF'
namespace fx {
constexpr int M = 2;
template <bool> struct LT {};
struct BP : LT<(M < 3)> {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
# ...and what the census does read inside a template list: a `<` after a literal is a
# comparison, and a lambda's `->` is not a `>`.
fixture "T68 a comparison after a literal in a template head is read" include/fx/h_tmpl_lit.hpp -std=c++17 \
  "^include/fx/h_tmpl_lit.hpp: fx::LC" <<'EOF'
namespace fx {
template <bool B = 1 < 2>
struct LC {
    int a = B;
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
};
}  // namespace fx
EOF
fixture "T69 a lambda with a trailing return type in a template head is read" include/fx/h_tmpl_arrow.hpp \
  -std=c++20 "^include/fx/h_tmpl_arrow.hpp: fx::LA" <<'EOF'
namespace fx {
template <auto F = []() -> int { return 1; }>
struct LA {
    int a = F();
#ifdef FIXPP_TEST_HOOKS
    int seeded = 0;
#endif
};
}  // namespace fx
EOF
# A requires-clause holding a token outside the clause's grammar. The census reads each kind of
# primary the grammar allows a requires-clause (a parenthesised expression, a requires-
# expression, an id-expression, a literal), so this fixture is ill-formed ON PURPOSE, and
# illformed() checks that it is: the cell pins the default branch, which is a refusal.
# illformed <name> <std> <file> — <file> must FAIL to compile in both states
illformed() {
  local name="$1" std="$2" f="$3" d
  [ -n "${ODR_CENSUS_UNDER_TEST:-}" ] && return 0
  for d in "" "-DFIXPP_TEST_HOOKS"; do
    if "$CXX" "$std" -fsyntax-only -x c++ $d "$f" >/dev/null 2>&1; then
      echo "FAIL  $name: its fixture compiles${d:+ with $d}, so it does not pin the default branch"
      fail=$((fail+1)); return 1
    fi
  done
}
t="$(mk t60)"; put "$t/include/fx/h_req_bad.hpp" <<'EOF'
namespace fx {
template <class T> concept RC = true;
template <class T>
requires !RC<T>
struct RB {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int seeded;
#endif
};
}  // namespace fx
EOF
illformed "T60 a requires-clause the census cannot read is refused" -std=c++20 "$t/include/fx/h_req_bad.hpp" \
  && check "T60 a requires-clause the census cannot read is refused" 2 "$t" \
    "ERROR include/fx/h_req_bad.hpp: a brace in a class head the census cannot read"

# ── an unnamed type is identified by the name it is declared with ────────────────────────────
# Paired by encounter order, a macro-only unnamed type earlier in the scope would shift the
# pairing of every later one.
fresh "T52 a typedef'd unnamed struct after a macro-only one is paired by its typedef name" \
  include/h_anon.hpp -std=c++17 1 "^include/h_anon.hpp: <anon:Second>" <<'EOF'
#ifdef FIXPP_TEST_HOOKS
typedef struct { int a; } First;
#endif
typedef struct {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int gated;
#endif
} Second;
EOF
fresh "T53 a macro-only typedef'd unnamed struct is one definition, not a divergence" \
  include/h_anon_only.hpp -std=c++17 0 "$NODIV" \
  "^include/h_anon_only.hpp: <anon:OnlyHooked> (only with FIXPP_TEST_HOOKS)" <<'EOF'
#ifdef FIXPP_TEST_HOOKS
typedef struct { int a; } OnlyHooked;
#endif
EOF
fresh "T63 an unnamed struct in an alias declaration after a macro-only one is paired by its alias" \
  include/h_anon_alias.hpp -std=c++17 1 "^include/h_anon_alias.hpp: <anon:Second>" <<'EOF'
#ifdef FIXPP_TEST_HOOKS
using First = struct { int a; };
#endif
using Second = struct {
    int a;
#ifdef FIXPP_TEST_HOOKS
    int gated;
#endif
};
EOF
# An unnamed enumeration has no such name, so it is paired by encounter order. That pairing is
# refused when it is ambiguous: two or more in one scope, and a different number in each state.
fresh "T54 unnamed enumerations whose number changes with the macro are refused" \
  include/fx/h_anon_enum.hpp -std=c++17 2 \
  "ERROR include/fx/h_anon_enum.hpp: definitions of fx::<anon> cannot be paired between the states (1 without FIXPP_TEST_HOOKS, 2 with)" <<'EOF'
namespace fx {
#ifdef FIXPP_TEST_HOOKS
enum { kA };
#endif
enum {
    kB,
#ifdef FIXPP_TEST_HOOKS
    kC,
#endif
};
}  // namespace fx
EOF
fresh "T55 one macro-only unnamed enumeration is one definition, not a divergence" \
  include/fx/h_anon_enum_one.hpp -std=c++17 0 "$NODIV" \
  "^include/fx/h_anon_enum_one.hpp: fx::<anon> (only with FIXPP_TEST_HOOKS)" <<'EOF'
namespace fx {
#ifdef FIXPP_TEST_HOOKS
enum { kOnly };
#endif
}  // namespace fx
EOF
fixture "T61 unnamed enumerations as many in both states are paired in order, and compared" \
  include/fx/h_anon_enum_two.hpp -std=c++17 "^include/fx/h_anon_enum_two.hpp: fx::<anon>#2" <<'EOF'
namespace fx {
enum { kA };
enum {
    kB,
#ifdef FIXPP_TEST_HOOKS
    kC,
#endif
};
}  // namespace fx
EOF
# Inside a type definition the enclosing definition is compared whole, so the pairing of the
# unnamed types in it is not refused: the enclosing one is the DIVERGENCE.
fixture "T66 unnamed enumerations inside a class are reported through the class" \
  include/fx/h_anon_nested.hpp -std=c++17 "^include/fx/h_anon_nested.hpp: fx::WithEnums" <<'EOF'
namespace fx {
struct WithEnums {
#ifdef FIXPP_TEST_HOOKS
    enum { kA };
#endif
    enum { kB };
};
}  // namespace fx
EOF

# ── a refusal is for the tree's own headers only ─────────────────────────────────────────────
# A third-party header cannot depend on the macro, so a head the rules cannot read there must
# not fail the scan (a dependency bump would turn it red). Here a header outside the source
# dir, reached through -isystem, holds T56's refused shape, and the header that includes it
# changes with the macro, so it is parsed in both states.
t="$(mk t67)"; ext="$TMP/t67ext"
put "$ext/ext_lt.hpp" <<'EOF'
#pragma once
namespace ext {
constexpr int M = 2;
template <int N = M < 3>
struct EL { int a = N; };
}  // namespace ext
EOF
put "$t/include/fx/uses_ext.hpp" <<'EOF'
#pragma once
#include <ext_lt.hpp>
#ifdef FIXPP_TEST_HOOKS
void seeded_ns_decl() noexcept;
#endif
EOF
db "$t" "$(cmd "$t" src/a.cpp "-isystem $ext"), $(cmd "$t" tests/t.cpp '-DFIXPP_TEST_HOOKS')"
if [ -n "${ODR_CENSUS_UNDER_TEST:-}" ] \
    || { "$CXX" -std=c++17 -isystem "$ext" -fsyntax-only -x c++ "$t/include/fx/uses_ext.hpp" \
         && "$CXX" -std=c++17 -isystem "$ext" -DFIXPP_TEST_HOOKS -fsyntax-only -x c++ \
              "$t/include/fx/uses_ext.hpp"; }; then
  check "T67 a head the rules cannot read in a THIRD-PARTY header is not refused" 0 "$t" \
    "$NODIV" "+ void seeded_ns_decl() noexcept;"
else
  echo "FAIL  T67 a head the rules cannot read in a THIRD-PARTY header: its fixture does not compile"
  fail=$((fail+1))
fi

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

# ── mutants ──────────────────────────────────────────────────────────────────────────────────
# A cell that cannot fail when the rule it names is broken measures its own setup. Each mutant
# is a copy of the census with one rule broken; the cell that names that rule runs against it
# (ODR_CELL), and must fail FOR ITS OWN REASON: its FAIL line must END with <reason>, the
# text that cell prints when only its rule is broken (a substring would accept a missing
# `fx::Outer::Inner` line for a cell whose reason is a missing `fx::Outer` line). A cell reddened by something else (a
# refusal turning its exit code into 2) does not count. A mutant whose anchor text is no
# longer in the census exactly once fails too, so a refactor cannot retire a mutant silently.
if [ -z "${ODR_CENSUS_UNDER_TEST:-}" ]; then
  out="$(ODR_CELL=T0 bash "${BASH_SOURCE[0]}" 2>&1)"; rc=$?
  if [ "$rc" = 1 ] && printf '%s\n' "$out" | grep -qF "ODR_CELL=T0 is set without ODR_CENSUS_UNDER_TEST"; then
    echo "ok    an ODR_CELL inherited by a run with no census under test is refused"; pass=$((pass+1))
  else
    echo "FAIL  an ODR_CELL inherited by a run with no census under test: exit $rc"
    printf '%s\n' "$out" | sed 's/^/      /' | head -6; fail=$((fail+1))
  fi
  # mutant <cell> <reason> <description> <python: old> <python: new>
  mutant() {
    local cell="$1" reason="$2" what="$3" m="$TMP/mutant.py" out line l own=""
    if ! python3 - "$HERE/odr-hooks-census.py" "$m" "$4" "$5" <<'PY'
import sys
src, dst, old, new = sys.argv[1:]
s = open(src).read()
if s.count(old) != 1 or old == new:
    sys.exit(1)
open(dst, "w").write(s.replace(old, new))
PY
    then
      echo "FAIL  mutant '$what': its anchor is not in the census exactly once"; fail=$((fail+1)); return
    fi
    out="$(ODR_CENSUS_UNDER_TEST="$m" ODR_CELL="$cell" bash "${BASH_SOURCE[0]}" 2>&1)"
    line="$(printf '%s\n' "$out" | grep "^FAIL  $cell ")"
    while IFS= read -r l; do
      [[ "$l" == *": $reason" ]] && own=1
    done <<<"$line"
    if [ -z "$line" ]; then
      echo "FAIL  mutant '$what' left $cell green"; fail=$((fail+1))
    elif [ -n "$own" ]; then
      echo "ok    mutant '$what' reddens $cell"; pass=$((pass+1))
    else
      echo "FAIL  mutant '$what' reddens $cell, but not for its own reason ($reason):"
      printf '%s\n' "$line" | sed 's/^/      /'; fail=$((fail+1))
    fi
  }
  # mutant_spec <cell> <reason> <token> — <token> dropped from DECL_SPECIFIERS
  DS="$(python3 -c 'import re, sys; print(re.search(r"DECL_SPECIFIERS = \([^)]*\)", open(sys.argv[1]).read()).group(0), end="")' "$HERE/odr-hooks-census.py")"
  mutant_spec() {
    local new
    new="$(python3 -c 'import re, sys; t, k = sys.argv[1:]; print(re.sub(r"\"%s\",\s*|,\s*\"%s\"(?=\))" % (k, k), "", t, count=1), end="")' "$DS" "$3")"
    mutant "$1" "$2" "'$3' is not skipped before the class key" "$DS" "$new"
  }
  X01="exit 0, wanted 1"
  mutant T5 "$X01" "the old rule: a head holding '=' or '(' is not a class" \
    '    """-> (kind, name) for the tokens before a `{`: read_head()'"'"'s, or ('"'"'expr'"'"', '"'"'paren'"'"')."""' \
    '    """-> (kind, name) for the tokens before a `{`: read_head()'"'"'s, or ('"'"'expr'"'"', '"'"'paren'"'"')."""
    if "=" in h or "(" in h:
        return "other", ""'
  mutant T6 "$X01" "the old rule, again: alignas(...) hides the class" \
    '    """-> (kind, name) for the tokens before a `{`: read_head()'"'"'s, or ('"'"'expr'"'"', '"'"'paren'"'"')."""' \
    '    """-> (kind, name) for the tokens before a `{`: read_head()'"'"'s, or ('"'"'expr'"'"', '"'"'paren'"'"')."""
    if "(" in h:
        return "other", ""'
  mutant T7 "$X01" "an enumeration is not a type definition" \
    '    if i >= len(h) or h[i] not in CLASS_KEYS + ("enum",):' \
    '    if i >= len(h) or h[i] not in CLASS_KEYS:'
  mutant T13 "exit 2, wanted 1" "a spelling of the macro is not stripped from the base flags" \
    '    return name_and_value.split("=", 1)[0] == MACRO' \
    '    return name_and_value == MACRO'
  mutant T10 "exit 1, wanted 0" "a difference outside any type definition fails the run" \
    '    return 1 if div else 0' \
    '    return 1 if div or other else 0'
  mutant T11 "exit 1, wanted 0" "a definition in ONE state only is reported as a divergence" \
    '        else:
            side = "with" if k in db else "without"' \
    '        else:
            div[k] = ((db if k in db else da)[k][2], [])
            side = "with" if k in db else "without"'
  mutant T8 "$X01" "only the BODY is compared, not the head" \
    '                toks_by_frame.append(list(head) + ["{"])' \
    '                toks_by_frame.append(["{"])'
  mutant T9 "exit 1 as wanted, but no line containing: ^include/fx/widget.hpp: fx::Outer" \
    "a nested definition is not part of its enclosing class" \
    '        for acc in toks_by_frame:' \
    '        for acc in toks_by_frame[-1:]:'
  mutant T23 "exit 2, wanted 1" "a raw string literal cannot span lines" \
    '(?:u8|u|U|L)?R"([^(\s]*)\((?:.|\n)*?\)\1"' \
    '(?:u8|u|U|L)?R"([^(\s]*)\(.*?\)\1"'
  mutant T25 "$X01" "a .h header is not scanned" \
    'HEADER_EXT = (".hpp", ".h", ".inl", ".ipp")' 'HEADER_EXT = (".hpp", ".inl", ".ipp")'
  mutant T26 "$X01" "an .inl fragment is not scanned" \
    'HEADER_EXT = (".hpp", ".h", ".inl", ".ipp")' 'HEADER_EXT = (".hpp", ".h", ".ipp")'
  mutant T27 "$X01" "an .ipp fragment is not scanned" \
    'HEADER_EXT = (".hpp", ".h", ".inl", ".ipp")' 'HEADER_EXT = (".hpp", ".h", ".inl")'
  mutant T28 "$X01" "'final' after the class name is not skipped" \
    '    if i < len(h) and h[i] == "final":
        i += 1
' ''
  mutant T29 "exit 1 as wanted, but no line containing: ^include/fx/h_access.hpp: fx::Holder::In" \
    "an access label is not skipped before the class key" \
    '        elif h[i] in ACCESS and i + 1 < len(h) and h[i + 1] == ":":' \
    '        elif False:'
  mutant_spec T30 "$X01" typedef
  mutant_spec T31 "$X01" static
  mutant_spec T32 "$X01" constexpr
  mutant_spec T33 "$X01" constinit
  mutant_spec T34 "$X01" const
  mutant_spec T35 "$X01" volatile
  mutant_spec T36 "$X01" inline
  mutant_spec T37 "$X01" thread_local
  mutant_spec T38 "exit 1 as wanted, but no line containing: ^include/fx/h_mutable.hpp: fx::MHolder::Mu" mutable
  mutant_spec T43 "$X01" __extension__
  mutant T39 "$X01" "'extern' without a linkage string is not skipped" \
    '            i += 1  # extern without a linkage string' \
    '            break'
  mutant T40 "$X01" "a linkage string before one declaration is read as a linkage block" \
    '            if i + 2 == len(h):' \
    '            if True:'
  mutant T41 "$X01" "'using A =' is not skipped" \
    '        elif h[i] == "using" and i + 2 < len(h) and h[i + 2] == "=":' \
    '        elif False:'
  mutant T42 "$X01" "attributes are not skipped between the specifiers" \
    '        if j != i:' \
    '        if False:'
  mutant T44 "exit 1, wanted 0" "the database's defines are not applied" \
    '                defs.append(flag)' \
    '                pass'
  mutant T45 "exit 1, wanted 0" "the entries are read in database order, not path order" \
    '    cxx_entries = sorted((e for e in db if e.get("file", "").endswith(CXX_EXT)),
                         key=lambda e: e["file"])' \
    '    cxx_entries = [e for e in db if e.get("file", "").endswith(CXX_EXT)]'
  # A brace in a class head: each rule broken alone. T46b sits inside both an open paren and a
  # base clause's open template list, so it is the end-to-end cell, with no mutant of its own.
  mutant T46a "exit 2, wanted 1" "a brace inside an open paren of a class head opens a scope" \
    '    if kind != "other" and paren_open(h):' \
    '    if False:'
  mutant T47 "$X01" "a brace inside a template head opens a scope" \
    '            i = skip_angle(h, i + 1)
            if i is None:
                return "expr", "angle"' \
    '            i = skip_angle(h, i + 1)
            if i is None:
                return "other", ""'
  mutant T62 "$X01" "a brace inside a class name's template arguments opens a scope" \
    '            if j is None:
                return "expr", "angle"
            name += h[i:j]' \
    '            if j is None:
                return "other", ""
            name += h[i:j]'
  # A type body read early is refused by what follows its `}`; this mutant is what shows it.
  mutant T48 "exit 2, wanted 1" "a brace inside a base clause's template argument opens a scope" \
    '    if i < len(h) and angle_depth(h, i + 1)[1]:' \
    '    if False:'
  mutant T49 "$X01" "a requires-expression's body before the class key opens a scope" \
    '            if why:
                return "expr", why
            templated = False' \
    '            if why == "angle":
                return "expr", why
            if why:
                return "other", ""
            templated = False'
  mutant T50 "$X01" "every brace in a head holding requires is an expression" \
    '    kind, name = read_head(h)
' \
    '    kind, name = read_head(h)
    if "requires" in h:
        return "expr", "requires"
'
  mutant T51 "exit 2, wanted 1" "template lists are counted in every head" \
    '    kind, name = read_head(h)
' \
    '    kind, name = read_head(h)
    if kind == "other" and angle_depth(h, 0)[1]:
        return "expr", "angle"
'
  mutant T56 "exit 0, wanted 2" "a template list still open where its head ends is not refused" \
    '        if classify_head(head) == ("expr", "angle"):' \
    '        if False:'
  mutant T58 "exit 0, wanted 2" "a base clause's template list still open where its head ends is not refused" \
    '        if classify_head(head) == ("expr", "angle"):' \
    '        if False:'
  mutant T57 "exit 2, wanted 1" "a parenthesised group inside a template list is not skipped" \
    '        if t in OPEN:
            j = skip_group(h, i)
            if j is None:
                return len(h), depth
            i = j
            continue' \
    '        if False:
            pass'
  mutant T59 "exit 2, wanted 1" "a parenthesised group inside a base clause's template list is not skipped" \
    '        if t in OPEN:
            j = skip_group(h, i)
            if j is None:
                return len(h), depth
            i = j
            continue' \
    '        if False:
            pass'
  mutant T68 "exit 2, wanted 1" "every < in a template list opens a nested one" \
    '        if t == "<" and i > 0 and IDENT.match(h[i - 1]):' \
    '        if t == "<":'
  mutant T69 "$X01" "a lambda's -> closes a template list" \
    '        if t == "-" and i + 1 < len(h) and h[i + 1] == ">":' \
    '        if False:'
  mutant T60 "exit 0, wanted 2" "a requires-clause the census cannot read is read as other" \
    '            if why == "unread":
                return "refuse", NO_READ' \
    '            if why == "unread":
                return "other", ""'
  mutant T64 "$X01" "one requires-clause ends the template heads" \
    '            templated = False  # one clause per template head' \
    '            break  # one clause per template head'
  mutant T65 "exit 2, wanted 1" "a head that starts with requires is read as a requires-clause" \
    '(h[i] == "template" or templated and h[i] == "requires")' \
    '(h[i] == "template" or h[i] == "requires")'
  mutant T67 "exit 2, wanted 0" "a head the rules cannot read is refused in a third-party header too" \
    '    return os.path.realpath(origin).startswith(CTX["src"])' \
    '    return True'
  # The identity of an unnamed type.
  mutant T52 "exit 2, wanted 1" "an unnamed struct is keyed by encounter order, not by its declarator" \
    '                    name = f"<anon:{d}>" if d else "<anon>"' \
    '                    name = "<anon>"'
  mutant T63 "exit 2, wanted 1" "an unnamed struct in an alias declaration is keyed by encounter order" \
    '    return "type", "<anon>" if is_enum else f"<anon:{alias}>" if alias else ""' \
    '    return "type", "<anon>" if is_enum else ""'
  mutant T54 "exit 1, wanted 2" "an ambiguous pairing by encounter order is not refused" \
    '        if na != nb and max(na, nb) >= 2 and any(own(o) for o in where):' \
    '        if False:'
  mutant T55 "exit 2, wanted 0" "one unnamed definition in one state is refused" \
    'if na != nb and max(na, nb) >= 2' 'if na != nb and max(na, nb) >= 1'
  mutant T61 "exit 2, wanted 1" "a pairing by encounter order is refused when both states hold as many" \
    'if na != nb and max(na, nb) >= 2' 'if max(na, nb) >= 2'
  mutant T66 "exit 2, wanted 1" "the pairing inside a type definition is refused too" \
    '            if not v[5]:' '            if True:'
  mutant T15 "exit 0, wanted 2" "a preprocessing error does not fail the run" \
    '    if refusals or errors or unread:' \
    '    if refusals or unread:'
  mutant T56 "exit 0, wanted 2" "a head the rules cannot read does not fail the run" \
    '    if refusals or errors or unread:' \
    '    if refusals or errors:'
  mutant T18 "exit 0, wanted 2" "an empty root is not refused" \
    '        if n == 0:' \
    '        if False:'
  mutant T21 "exit 0, wanted 2" "the positive control is not checked" \
    '    if r["status"] != "DIFF" or list(r["div"]) != [PROBE_KEY]:' \
    '    if False:'
fi

echo
echo "test-odr-hooks-census: $pass passed, $fail failed (CXX=$CXX)"
[ "$fail" = 0 ] && [ "$pass" -gt 0 ]
