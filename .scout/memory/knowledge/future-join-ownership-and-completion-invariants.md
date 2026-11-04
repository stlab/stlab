---
title: "Future Join Ownership and Completion Invariants"
entity_type: decision
confidence: 0.99
created: 2026-10-29T22:35:37Z
last_accessed: 2026-10-29T22:35:37Z
last_reinforced: 2026-10-29T22:35:37Z
access_count: 1
sources:
  - "include/stlab/concurrency/future.hpp:1394-1412"
  - "include/stlab/concurrency/future.hpp:1414-1461"
  - "include/stlab/concurrency/future.hpp:1464-1473"
  - "include/stlab/concurrency/future.hpp:1476-1503"
  - "https://github.com/stlab/stlab/pull/612"
tags: ["future", "when_all", "when_any", "ownership", "broken-promise", "concurrency"]
tier: knowledge
source: Verified implementation at fix commit b189cdf68f7286da9dcbbc5cc11b64621265a105, merged by PR #612 as 35197e5d81ebf6afe4a74dd833fd34a5ff55d005; historical revision read directly because it was not indexed by Scout.
---

# Future Join Ownership and Completion Invariants

The issue #610 fix deliberately avoids adding a `recover()` shared state per input. The old executor-scheduled recovery notifications could be discarded, leaving `broken_promise` in an unread intermediate future while the aggregate remained pending. Preserve the owned-input/direct-completion design introduced by #612 rather than layering more recovery futures onto joins.

`when_shared<Inputs, Any>` owns the original input futures and one final packaged task (`include/stlab/concurrency/future.hpp:1394-1412`). `attach_when_inputs` installs immediate `on_completion` observers capturing a weak aggregate, a pointer to its stable owned input, and the input index, then ends registration (`include/stlab/concurrency/future.hpp:1464-1473`). Input storage must remain stable throughout observer installation.

The registration barrier is essential: already-ready inputs may notify synchronously, but neither dispatch nor input release may occur before every observer is attached. Under `_guard`, `_decided` makes the terminal decision immutable and is checked before reading an input; this prevents late callbacks from touching inputs that dispatch has reset. `when_all` decides on the first failure or all successful completions; `when_any` decides on the first observed success or the last failure when all fail (`include/stlab/concurrency/future.hpp:1424-1461`).

Move the final packaged task under the decision lock, then release losing inputs and submit the task outside that lock (`include/stlab/concurrency/future.hpp:1414-1422,1446-1460`). Completion bookkeeping must not extract values or invoke the user callable; extraction remains in the final task's apply helpers (`include/stlab/concurrency/future.hpp:1476-1503`). This separation preserves executor routing while avoiding per-input notification shared states.

Line references are verified against commit `b189cdf68f7286da9dcbbc5cc11b64621265a105`, merged by #612.
