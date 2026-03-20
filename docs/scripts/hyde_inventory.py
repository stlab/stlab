#!/usr/bin/env python3
"""Scan docs/include/stlab/**/*.md for Hyde front matter + body; emit inventory report."""
from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("Install PyYAML: pip install pyyaml", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parents[2]  # repo root
INCLUDE_MD = ROOT / "docs" / "include" / "stlab"


def split_front_matter(text: str) -> tuple[dict | None, str]:
    if not text.startswith("---"):
        return None, text
    parts = text.split("---", 2)
    if len(parts) < 3:
        return None, text
    try:
        meta = yaml.safe_load(parts[1])
    except yaml.YAMLError:
        return None, text
    body = parts[2].lstrip("\n")
    return meta if isinstance(meta, dict) else None, body


def hyde_brief(meta: dict) -> str:
    h = meta.get("hyde")
    if not isinstance(h, dict):
        return ""
    b = h.get("brief")
    return "" if b is None else str(b)


def has_substantive_body(body: str) -> bool:
    b = body.strip()
    if not b:
        return False
    # ignore whitespace-only or trivial separators
    if re.fullmatch(r"[\s\*\-]+", b):
        return False
    return True


def infer_header(md_path: Path) -> str:
    rel = md_path.relative_to(INCLUDE_MD)
    parts = rel.parts
    for i, p in enumerate(parts):
        if p.endswith(".hpp"):
            return "include/stlab/" + "/".join(parts[: i + 1])
    return ""


def main() -> None:
    rows: list[tuple[str, str, str, bool, str, str]] = []
    for md in sorted(INCLUDE_MD.rglob("*.md")):
        text = md.read_text(encoding="utf-8", errors="replace")
        meta, body = split_front_matter(text)
        rel = str(md.relative_to(ROOT)).replace("\\", "/")
        header = infer_header(md)
        if not header and meta:
            h = meta.get("hyde")
            if isinstance(h, dict) and h.get("defined_in_file"):
                header = "include/stlab/" + str(h["defined_in_file"]).replace("\\", "/")
        symbol = ""
        if meta:
            symbol = str(meta.get("title", ""))
        hb = hyde_brief(meta) if meta else ""
        body_ok = has_substantive_body(body)
        needs = body_ok or (
            hb
            and hb not in ("__INLINED__", "__MISSING__", "__OPTIONAL__", "_multiple descriptions_")
        )
        rows.append((rel, header, symbol, body_ok, hb, "yes" if needs else "no"))

    out = ROOT / "docs" / "hyde_source_inventory.md"
    lines = [
        "# Hyde → source inventory (auto-generated)",
        "",
        "| md_path | header | symbol_hint | has_md_body | hyde.brief | review |",
        "|---------|--------|-------------|-------------|------------|--------|",
    ]
    for rel, header, symbol, body_ok, hb, review in rows:
        hb_e = hb.replace("|", "\\|")[:80]
        sym_e = symbol.replace("|", "\\|")[:60]
        lines.append(
            f"| `{rel}` | `{header}` | `{sym_e}` | {body_ok} | `{hb_e}` | {review} |"
        )
    lines.append("")
    lines.append(f"Total files: {len(rows)}")
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {out} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
