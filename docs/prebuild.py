#!/usr/bin/env python3
"""
prebuild.py — populate docs/src/ with whitelisted content before `mdbook build`.

Run from the repository root (i.e. the directory that contains `spec/` and `specs/`):

    python3 docs/prebuild.py

Or from any directory by passing --root:

    python3 docs/prebuild.py --root /path/to/fixpp

Idempotent: cleans generated content in docs/src/ before regenerating.
Exits non-zero on any error with a clear message.

Strict allowlist (deny-by-default):
  * spec/feature-catalogue.md  → docs/src/feature-catalogue.md
  * specs/<id>/{spec,plan,research,data-model,quickstart}.md
  * specs/<id>/contracts/*.h and *.hpp
  * specs/<id>/checklists/*.md

Hard-excluded regardless of allowlist:
  * Anything under .specify/
  * Anything matching no-research.yml rejection patterns:
      ^research/ | ^decisions/ | ^book/ | .*-decisions\\.md$ | ^opus_plan\\.md$ | ^SYNTHESIS\\.md$
  * Any filename matching: *review*, *decision*, notes.md, WIP.md
"""

import argparse
import re
import shutil
import sys
from collections import Counter
from pathlib import Path


# ---------------------------------------------------------------------------
# Hard-exclusion rules (must match no-research.yml)
# ---------------------------------------------------------------------------

_FORBIDDEN_PATTERNS = [
    re.compile(r"^research/"),
    re.compile(r"^decisions/"),
    re.compile(r"^book/"),
    re.compile(r".*-decisions\.md$"),
    re.compile(r"^opus_plan\.md$"),
    re.compile(r"^SYNTHESIS\.md$"),
]

_FORBIDDEN_FILENAME_PATTERNS = [
    re.compile(r".*review.*", re.IGNORECASE),
    re.compile(r".*decision.*", re.IGNORECASE),
    re.compile(r"^notes\.md$", re.IGNORECASE),
    re.compile(r"^WIP\.md$", re.IGNORECASE),
]

# Paths that must never be traversed (defence-in-depth)
_FORBIDDEN_DIRS = {".specify"}


def _is_forbidden(rel_path: str) -> bool:
    """Return True if rel_path must never appear in docs/src/."""
    # Check against no-research.yml patterns
    for pat in _FORBIDDEN_PATTERNS:
        if pat.search(rel_path):
            return True
    # Check filename against defence-in-depth patterns
    filename = Path(rel_path).name
    for pat in _FORBIDDEN_FILENAME_PATTERNS:
        if pat.match(filename):
            return True
    # Check parent directories
    parts = Path(rel_path).parts
    for part in parts:
        if part in _FORBIDDEN_DIRS:
            return True
    return False


def _assert_not_forbidden(rel_path: str, dst: Path) -> None:
    """Fail loudly if a file about to be written to docs/src/ is forbidden."""
    if _is_forbidden(rel_path):
        sys.exit(
            f"ERROR: prebuild.py attempted to copy a forbidden path: {rel_path!r} -> {dst}\n"
            "This is a bug in prebuild.py's allowlist. Aborting."
        )


# ---------------------------------------------------------------------------
# Status-at-a-glance parser
# ---------------------------------------------------------------------------

def _status_header(catalogue_text: str) -> str:
    """
    Parse all Markdown tables in catalogue_text, find the Status column,
    and return a '## Status at a glance' section.
    Returns a fallback message if parsing fails.
    """
    # Scan all table rows (lines that start and end with |)
    # The Status column header is literally "Status"
    status_col_index: int | None = None
    counts: Counter = Counter()

    header_re = re.compile(r"^\|(.+)\|$")
    separator_re = re.compile(r"^\|[\s\-|:]+\|$")

    lines = catalogue_text.splitlines()
    in_table = False
    current_header_cols: list[str] = []

    for line in lines:
        stripped = line.strip()
        m = header_re.match(stripped)
        if not m:
            in_table = False
            current_header_cols = []
            status_col_index = None
            continue

        cells = [c.strip() for c in stripped.split("|")[1:-1]]

        if separator_re.match(stripped):
            # separator row — table header was the line before
            continue

        # Is this a header row? (contains "Status" as a cell)
        if "Status" in cells:
            current_header_cols = cells
            status_col_index = cells.index("Status")
            in_table = True
            continue

        # Data row in an active table
        if in_table and status_col_index is not None:
            if len(cells) > status_col_index:
                raw_status = cells[status_col_index].strip()
                if raw_status and raw_status != "—":
                    counts[raw_status] += 1

    if not counts:
        return (
            "## Status at a glance\n\n"
            "*Auto-summary unavailable (could not parse status column).*\n"
        )

    # Build table sorted by count descending, then alphabetically
    rows = sorted(counts.items(), key=lambda x: (-x[1], x[0]))
    total = sum(counts.values())

    lines_out = [
        "## Status at a glance\n",
        "| Status | Count |",
        "| --- | --- |",
    ]
    for status, count in rows:
        lines_out.append(f"| {status} | {count} |")
    lines_out.append(f"| **Total** | **{total}** |")
    lines_out.append("")

    return "\n".join(lines_out) + "\n"


# ---------------------------------------------------------------------------
# SUMMARY.md generator
# ---------------------------------------------------------------------------

_SPEC_PROSE_FILES = ["spec.md", "plan.md", "research.md", "data-model.md", "quickstart.md"]


def _generate_summary(src_dir: Path, spec_ids: list[str]) -> str:
    """Build SUMMARY.md content from discovered content."""
    lines = [
        "# Summary\n",
        "- [Introduction](./intro.md)",
        "- [Feature catalogue](./feature-catalogue.md)",
    ]

    # Static hand-authored top-level pages under docs/src/ (tracked in git
    # alongside intro.md; not managed by prebuild.py copy logic).
    if (src_dir / "tls-quickstart.md").exists():
        lines.append("- [TLS quickstart](./tls-quickstart.md)")
    if (src_dir / "transport-quickstart.md").exists():
        lines.append("- [Transport quickstart](./transport-quickstart.md)")

    # Static hand-authored docs under docs/src/dictionary/ (tracked in git,
    # not managed by prebuild.py copy logic).
    dict_dir = src_dir / "dictionary"
    if dict_dir.is_dir():
        dict_files = sorted(f for f in dict_dir.iterdir() if f.suffix == ".md")
        if dict_files:
            lines.append("- [Dictionary]()")
            for df in dict_files:
                label = df.stem.replace("-", " ").replace("_", " ").title()
                lines.append(f"  - [{label}](./dictionary/{df.name})")

    lines.append("- [Feature specs]()")

    for spec_id in sorted(spec_ids):
        spec_dir = src_dir / "specs" / spec_id

        # Primary entry point: spec.md (required for spec to appear at all)
        spec_md = spec_dir / "spec.md"
        if not spec_md.exists():
            continue

        lines.append(f"  - [{spec_id}](./specs/{spec_id}/spec.md)")

        # Optional prose files (skip spec.md — already the section root)
        for fname in _SPEC_PROSE_FILES[1:]:
            if (spec_dir / fname).exists():
                label = fname.replace(".md", "").replace("-", " ").title()
                lines.append(f"    - [{label}](./specs/{spec_id}/{fname})")

        # Contracts
        contracts_dir = spec_dir / "contracts"
        if contracts_dir.is_dir():
            contract_files = sorted(
                f for f in contracts_dir.iterdir()
                if f.suffix in {".h", ".hpp", ".md"}
            )
            for cf in contract_files:
                lines.append(
                    f"    - [{cf.name}](./specs/{spec_id}/contracts/{cf.name})"
                )

        # Checklists
        checklists_dir = spec_dir / "checklists"
        if checklists_dir.is_dir():
            checklist_files = sorted(
                f for f in checklists_dir.iterdir() if f.suffix == ".md"
            )
            for clf in checklist_files:
                lines.append(
                    f"    - [{clf.name}](./specs/{spec_id}/checklists/{clf.name})"
                )

    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Main copy logic
# ---------------------------------------------------------------------------

def _copy_file(src: Path, dst: Path, rel_path: str) -> None:
    """Copy src to dst, creating parent directories as needed."""
    _assert_not_forbidden(rel_path, dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def _clean_generated(src_dir: Path) -> None:
    """
    Remove generated content from docs/src/:
      - feature-catalogue.md
      - specs/
      - SUMMARY.md
    Leaves intro.md (static, hand-written) alone.
    """
    for name in ["feature-catalogue.md", "SUMMARY.md"]:
        p = src_dir / name
        if p.exists():
            p.unlink()
    specs_dst = src_dir / "specs"
    if specs_dst.is_dir():
        shutil.rmtree(specs_dst)


def main() -> None:
    parser = argparse.ArgumentParser(description="Populate docs/src/ for mdBook.")
    parser.add_argument(
        "--root",
        default=".",
        help="Repository root (default: current directory)",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    docs_src = root / "docs" / "src"
    spec_catalogue = root / "spec" / "feature-catalogue.md"
    specs_root = root / "specs"

    # Validate root looks right
    if not spec_catalogue.exists():
        sys.exit(
            f"ERROR: {spec_catalogue} not found. "
            "Run prebuild.py from the repository root (or pass --root)."
        )

    # Ensure docs/src exists
    docs_src.mkdir(parents=True, exist_ok=True)

    # --- Clean generated content (idempotent) ---
    print("Cleaning generated content from docs/src/ ...")
    _clean_generated(docs_src)

    # --- Copy feature-catalogue.md with status header prepended ---
    print(f"Processing {spec_catalogue.relative_to(root)} ...")
    catalogue_text = spec_catalogue.read_text(encoding="utf-8")
    status_section = _status_header(catalogue_text)
    combined = status_section + "\n" + catalogue_text
    dst_catalogue = docs_src / "feature-catalogue.md"
    _assert_not_forbidden("spec/feature-catalogue.md", dst_catalogue)
    dst_catalogue.write_text(combined, encoding="utf-8")

    # --- Copy specs/ content ---
    spec_ids: list[str] = []

    if specs_root.is_dir():
        for spec_dir in sorted(specs_root.iterdir()):
            if not spec_dir.is_dir():
                continue
            spec_id = spec_dir.name

            # Skip .specify or any hidden directory
            if spec_id.startswith("."):
                continue

            copied_any = False

            # Prose files
            for fname in _SPEC_PROSE_FILES:
                src_file = spec_dir / fname
                if src_file.exists():
                    rel = f"specs/{spec_id}/{fname}"
                    dst_file = docs_src / "specs" / spec_id / fname
                    _copy_file(src_file, dst_file, rel)
                    print(f"  Copied {rel}")
                    copied_any = True

            # Contracts: *.h, *.hpp
            contracts_src = spec_dir / "contracts"
            if contracts_src.is_dir():
                for f in sorted(contracts_src.iterdir()):
                    if f.suffix in {".h", ".hpp"}:
                        rel = f"specs/{spec_id}/contracts/{f.name}"
                        dst_file = docs_src / "specs" / spec_id / "contracts" / f.name
                        _copy_file(f, dst_file, rel)
                        print(f"  Copied {rel}")
                        copied_any = True

            # Checklists: *.md
            checklists_src = spec_dir / "checklists"
            if checklists_src.is_dir():
                for f in sorted(checklists_src.iterdir()):
                    if f.suffix == ".md":
                        rel = f"specs/{spec_id}/checklists/{f.name}"
                        dst_file = (
                            docs_src / "specs" / spec_id / "checklists" / f.name
                        )
                        _copy_file(f, dst_file, rel)
                        print(f"  Copied {rel}")
                        copied_any = True

            if copied_any:
                spec_ids.append(spec_id)

    # --- Generate SUMMARY.md ---
    print("Generating SUMMARY.md ...")
    summary_text = _generate_summary(docs_src, spec_ids)
    summary_dst = docs_src / "SUMMARY.md"
    _assert_not_forbidden("docs/src/SUMMARY.md", summary_dst)
    summary_dst.write_text(summary_text, encoding="utf-8")

    print(f"\nOK — docs/src/ populated ({len(spec_ids)} spec(s): {', '.join(sorted(spec_ids)) or 'none'}).")
    print("Next: mdbook build docs")


if __name__ == "__main__":
    main()
