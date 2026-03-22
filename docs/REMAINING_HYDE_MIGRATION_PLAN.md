# Plan: Finish migrating Hyde docs into sources (no Hyde maintenance)

## Goals

- **Retire** [`docs/include/`](../include/) as a maintained documentation source: do not edit Hyde/Jekyll pages going forward.
- Move remaining **narrative Markdown** (library `index.md` bodies, long-form paragraphs, examples) into **Doxygen-visible** places: primarily `@file` / `@defgroup` **detailed descriptions** and, where appropriate, class or function comment blocks in existing headers.
- **Do not add new C++ headers** unless absolutely necessary; prefer enriching existing [`include/stlab/**/*.hpp`](../include/stlab) and the existing Doxygen-only helper [`docs/doxygen/stlab_groups.hpp`](doxygen/stlab_groups.hpp).

## Non-goals

- Re-running Hyde to refresh YAML (`inline:` / `__INLINED__` alignment).
- Preserving Jekyll-specific links (`./f_*.html`); rewrite to backticks, `\ref`, or plain prose.

## Where content should live

| Hyde source pattern | Target in repo |
|---------------------|----------------|
| `…/<header>.hpp/index.md` body (CSP, thread pool, etc.) | Expand that header’s `/*! @file … */` with `@brief` + `@details` (Markdown allowed in Doxygen 1.8+). |
| Section overview (`stlab/index.md`, `concurrency/index.md`, …) | **Doxygen:** add `@details` on existing `@defgroup` entries in [`stlab_groups.hpp`](doxygen/stlab_groups.hpp) (`stlab`, `stlab_concurrency`, `stlab_test`, …). **Human:** one short pointer in [`README.md`](../README.md) if something is user-facing and not API-only. |
| Class / enum / free-function Hyde pages | Matching declaration in the **same `.hpp`**: `/** … */` immediately above the entity; use `@brief` + `@details` for long text. |
| Duplicate “Constructor” / “Equality comparator” one-liners | **Batch:** either skip as noise or fold into a **single** class-level `@details` instead of 20 identical lines. |
| Examples in Markdown | `@par Example` + `@code` / `@endcode` next to the relevant API (keep examples minimal and honest about includes). |

**Optional without new source files:** enrich `@defgroup stlab_concurrency` (and siblings) in `stlab_groups.hpp` with `@details` copied from old section `index.md` files—this gives a “concurrency chapter” landing page in Doxygen Modules.

## Constraints and quality bar

- **Math in comments:** use Doxygen math markup (`\f$…\f$`, `\f[…\f]`). HTML output uses **MathJax 3** ([`docs/Doxyfile`](Doxyfile) `USE_MATHJAX`); no LaTeX install is required. For prose-only complexity, plain English is still fine.
- Keep **`stlab::detail`** excluded: do not document implementation internals unless policy changes.
- After each chunk: **`cmake --preset=debug-portable` + `cmake --build … --target stlab`** and **`doxygen`** on the configured `Doxyfile`; fix new warnings.

## Phased execution (recommended order)

### Phase 1 — Module and file overviews (high leverage, small diff) — **done**

1. From Hyde **`index.md` bodies**, expand `@file` blocks for headers that still lack the narrative: e.g. [`channel.hpp`](../include/stlab/concurrency/channel.hpp) (CSP + processing graphs), confirm [`default_executor.hpp`](../include/stlab/concurrency/default_executor.hpp), [`await.hpp`](../include/stlab/concurrency/await.hpp), [`future.hpp`](../include/stlab/concurrency/future.hpp) top-level `index` themes.
2. Add **`@details`** to **`@defgroup stlab`**, **`stlab_concurrency`**, **`stlab_test`**, **`stlab_algorithm`** in [`stlab_groups.hpp`](doxygen/stlab_groups.hpp) using text from `docs/include/stlab/index.md`, `concurrency/index.md`, `test/index.md`, `algorithm/index.md`.
3. Update [`README.md`](../README.md) only if a non-Doxygen audience still needs a one-paragraph “what is stlab” (avoid duplicating entire Hyde pages).

### Phase 2 — `forest.hpp` (largest remaining volume) — **done**

Use [`docs/hyde_source_inventory.md`](hyde_source_inventory.md) as a **checklist** (read-only).

1. **Tier A:** `forest` class public API: methods with Hyde prose (`insert`, `splice`, iterators, `child_range`, etc.)—one block per logical group where Hyde duplicated boilerplate.
2. **Tier B:** Supporting types (`child_iterator`, `edge_iterator`, `child_adaptor`, …): class-level `@brief` + `@details` summarizing purpose; add member `@brief` only where Hyde had non-generic text.
3. **Tier C:** Free functions with extra Hyde body (`find_parent`, range helpers): ensure `@file` or function `@details` cover algorithmic / complexity notes already agreed in earlier migration.

**Done in source:** `@file` / `@defgroup` narratives; class-level doc for `forest<T>` (fullorder, size, erase semantics); iterator adaptor briefs; edge/pivot helpers; `find_parent` / `erase` / `size` details; range helpers (`child_*`, `preorder_range`, …). Member-level Hyde boilerplate skipped per plan.

### Phase 3 — `future.hpp` — **done**

1. File-level: ensure the big module comment + `@file` reflect Hyde library themes (lifecycle, cancellation, coroutines)—merge, don’t duplicate paragraph-for-paragraph if already present.
2. Free functions / class templates still only documented in Hyde: `when_all`, error handling, deprecated paths—attach **`@deprecated`** + `@details` in source to match Hyde intent.
3. **Overload-heavy** operators (`==`, `|`): prefer **one** grouped `/** @{ */` comment or `@overload` pattern consistent with the file.

**Done in source:** `@defgroup` `@details` (pointer to file narrative); `when_all` / `when_any` / `async` `@details`; `package` brief; `\deprecated` on `error()`; `operator==`/`!=` grouped with `@{` `@}`; `@name` subsection for pipe operators on both `future` specializations.

### Phase 4 — `channel.hpp`

1. `sender`, `receiver`, `buffer_size`, process types: class-level docs from Hyde `index.md` + critical methods (`operator()`, `operator|`, `close`, `ready`).
2. Deprecations (`join`, `merge` vs `merge_channel`, etc.): mirror Hyde warnings in `@deprecated` / `@details`.

### Phase 5 — Smaller headers and leftovers

- [`forest_algorithms.hpp`](../include/stlab/forest_algorithms.hpp): `@file` + `@details` from Hyde `forest_algorithms` pages; briefs on `equal_shape`, `transcribe`, `flatten`, `unflatten` as needed.
- [`serial_queue.hpp`](../include/stlab/concurrency/serial_queue.hpp), [`task.hpp`](../include/stlab/concurrency/task.hpp), [`progress.hpp`](../include/stlab/concurrency/progress.hpp), etc.: only where inventory shows substantive Hyde-only text.
- [`test/model.hpp`](../include/stlab/test/model.hpp): class-level `@details` from Hyde class `index.md` bodies if still missing.

### Phase 6 — Remove the old site tree

1. When satisfied with Doxygen (and README) coverage, **delete** `docs/include/` (or replace with a short `docs/include/README.md` stating “removed; see Doxygen + headers”).
2. Adjust Jekyll config ([`_config.yml`](_config.yml)) so the site no longer expects `hyde_yaml_dir` content, or redirect GitHub Pages to **Doxygen-only** layout—coordinate with whoever publishes the site.

## Tracking

- Keep [`docs/hyde_source_inventory.md`](hyde_source_inventory.md) as a **static snapshot** or regenerate once from [`docs/scripts/hyde_inventory.py`](scripts/hyde_inventory.py) before deleting Hyde files, to preserve a paper trail.
- Optionally add a checkbox section at the bottom of this file per phase.

## When a *new* file might be justified (only if you later choose)

If Doxygen needs a **standalone main page** with long narrative and you refuse to grow `stlab_groups.hpp`: a **single** `docs/doxygen/mainpage.md` with `@mainpage` is the smallest addition (still not a compiled `.hpp`). **Default recommendation:** avoid; use `stlab_groups.hpp` + README instead.
