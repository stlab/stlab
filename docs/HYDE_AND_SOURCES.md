# Hyde (`docs/include`) and header comments

API prose should live in `include/stlab/**/*.hpp` as Doxygen comments (`///`, `/** */`) so **Doxygen** and **IDE tooltips** stay authoritative.

The Jekyll/Hyde tree under `docs/include/` is generated or hand-edited for the website. After enriching comments in headers, **regenerate Hyde** from sources when your toolchain supports it, so YAML `inline:` fields and `__INLINED__` markers stay aligned and long-form Markdown bodies do not drift as the only copy of the documentation.

The inventory script [`scripts/hyde_inventory.py`](scripts/hyde_inventory.py) lists Hyde pages with extra Markdown bodies or non-inlined briefs (output: [`hyde_source_inventory.md`](hyde_source_inventory.md)).
