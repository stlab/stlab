---
title: "CPM shorthand consumption differs from bundled installation"
entity_type: discovery
confidence: 0.95
created: 2026-11-03T22:36:30Z
last_accessed: 2026-11-03T22:44:37Z
last_reinforced: 2026-11-03T22:44:37Z
access_count: 2
sources:
  - "test\\\\verify_execution_packages.cmake:170-182"
  - ".cache\\\\cpm\\\\cpm\\\\CPM_0.43.2.cmake:678-690"
  - "test\\verify_execution_packages.cmake:131-147"
  - "test\\verify_execution_packages.cmake:190-239"
  - "stlab/cpp-library:cmake\\cpp-library-install.cmake:459-508"
  - "stlab/cpp-library:cmake\\cpp-library-install.cmake:392-407"
  - "stlab/cpp-library:tests\\install\\fixtures\\nested\\CMakeLists.txt:22"
tags: ["cmake", "cpm", "installation", "dependencies", "extraction"]
tier: knowledge
---

# CPM shorthand consumption differs from bundled installation

The execution extraction exposed a general cpp-library installation integration gap, not an execution-specific dependency requirement. test\verify_execution_packages.cmake:131-147 calls its both-options-enabled case 'combined': it explicitly sets STLAB_INSTALL and STLAB_EXECUTION_INSTALL then runs one parent install. The test also checks independently installed dependency consumption (190-207), header ownership (211-230), and linkage independence (234-239). cpp-library's cmake\cpp-library-install.cmake:459-470 honors the install option by registering normal directory-local install rules (477-508); it generates find_dependency calls (392-407) rather than acquiring/installing dependencies itself. CPM shorthand sets EXCLUDE_FROM_ALL YES, so CMake omits those child rules from the parent install even if the package's install option is ON. cpp-library's nested regression uses plain add_subdirectory(leaf), not CPM shorthand (tests\install\fixtures\nested\CMakeLists.txt:22), leaving this boundary untested. Before extraction, STLab already used bare shorthand for cow/enum-ops; that establishes prior consumption support, not automatic dependency installation. Explicit EXCLUDE_FROM_ALL NO is a workaround, not an agreed design; SYSTEM NO has no demonstrated necessity. Generic toolkit support and corresponding CPM-aware tests remain unresolved.
