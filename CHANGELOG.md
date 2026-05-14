## Unreleased

- Renamed root documentation files to Markdown and consolidated the active
  roadmap into `README.md`, removing the standalone TODO file.
- Added VSM cost analysis modes:
  - `--cost` for human-readable reports;
  - `--cost-json` for machine-readable reports;
  - `--cost-loop <label>=<count>` for loop-weighted block analysis;
  - `--cost-loop-preset ps2gl` for known ps2gl hot-loop weights.
- Cost reports now include static cycles, estimated cycles, FDIV/EFU producer
  issue stalls, explicit `waitq`/`waitp` stalls, slot usage, paired cycles,
  NOP slots, per-label block costs, and weighted hot-block rankings.
- Added regression fixtures and unit/integration tests for cost analysis.
- Added conservative VU scheduling improvements used by ps2gl:
  - upper/lower pairing lookahead;
  - latency-gap filling;
  - deferred Q/P waits;
  - Q-consuming FMAC pairing with deferred `waitq`;
  - safe plain-store and selected memory movement;
  - branch-padding reuse;
  - conservative branch-delay filling for independent integer instructions;
  - standalone branches omit the old extra pre-branch bubble after normal
    read-hazard padding is satisfied;
  - pre-increment plain stores can fill branch delay slots by adjusting their
    memory offset against the incremented base register;
  - independent plain stores can fill loop branch delay slots after the
    loop-counter increment when they do not read the updated counter;
  - `lq`/`lqi`/`lqd` results can feed `ftoi*` conversions through a narrow
    bypass matching SCE ps2gl ADC setup output;
  - dead VI-only fallthrough integer instructions can fill forward conditional
    branch delay slots when the taken path overwrites the same VI value before
    reading it;
  - deterministic alias allocation;
  - per-field VF readiness tracking;
  - disjoint VF field read/write pairing;
  - terminal-branch auto-exit suppression.
- Fixed several scheduler correctness bugs found through ps2gl/PCSX2 testing:
  - unsafe `loi` pairing with FMAC instructions that read `I`;
  - implicit broadcast reads, such as `mulw.xyz ..., VFw`, now depend on the
    broadcast component instead of the destination `.xyz` mask;
  - Q/P producer and consumer accounting now treats `mfp` as a P consumer, not
    a new P producer.
  - `madd*` and `msub*` instruction metadata now models ACC reads, preventing
    scheduler movement across multiply-add/subtract accumulator chains.
- Added focused regression tests for the new scheduling hazards.
- Established the next architecture direction: one canonical VU instruction
  metadata table shared by the scheduler and cost analyzer.
- Introduced `VuInstructionInfo` as the first shared VU instruction metadata
  table and moved cost-analyzer opcode classification, latency, throughput,
  and Q/P producer checks onto it.
- Expanded `VuInstructionInfo` into the canonical parser/cost metadata table,
  moved hardware operand construction onto it, and added
  `--dump-instruction-info` / `--dump-instruction-info-json`.
- Added `VuTokenResourceAccess` as the table-driven descriptor layer for VF/VI
  register fields, implicit resources, memory flags, branch delay slots, and
  bypass notes.
- Moved scheduler register-key, field-mask, Q/P, and implicit-resource checks
  onto `VuTokenResourceAccess`.
- Moved CodeGenerator pairing and movement register-conflict checks onto
  `VuTokenResourceAccess` read/write descriptors.
- Moved scheduler memory/control classification for loads, stores, `xgkick`,
  pre/post-increment, and branch barriers onto `VuTokenResourceAccess`.
- Added memory base-register and constant-offset descriptors and moved the
  scheduler's plain memory alias checks onto them.
- Added branch behavior flags for unconditional, link, and register branches,
  and moved branch-delay emission/pairing checks onto descriptor metadata.
- Added `VuSchedulingRules` as the shared stateless rule layer for emittable
  token checks, token movement, pair resource conflicts, Q/P and MAC/CLIP flag
  predicates, branch-delay queries, and adjacent integer-add coalescing.
- Moved `VuSchedulerAnalysis` ready-candidate, barrier, memory-ordering, pipe,
  and latency-priority checks onto `VuSchedulingRules`.
- Let the ready-set scheduler include dependency-safe plain stores so
  long-latency Q/P producers can move ahead of them when descriptors prove the
  movement safe.
- Let the ready-set dependency graph distinguish plain memory accesses by base
  register and constant offset, allowing distinct loads/stores to reorder.
- Added dependency-chain priority to the ready-set scheduler so critical
  producer chains are chosen ahead of unrelated short work.
- Weighted ready-set dependency-chain priority by instruction latency, improving
  the ps2gl pure-OpenVCL aggregate by 2 static and 2 estimated cycles.
- Added deferred-wait pairing for independent upper-pipe work above lower-pipe
  Q/P consumers, allowing a movable upper instruction to share the `waitq` or
  `waitp` row.
- Ignored MAC flag WAW edges in ready-set scheduling when the full shader never
  reads MAC flags, improving the ps2gl pure-OpenVCL aggregate by another 84
  static and 83 estimated cycles.
- Added the same dead-reader detection for CLIP flags, keeping CLIP WAW
  ordering only when a shader can read CLIP state.
- Reused the same dead MAC/CLIP WAW mask in CodeGenerator latency-gap filling
  and pairing lookahead, improving the ps2gl pure-OpenVCL aggregate by another
  28 static and 32 estimated cycles.
- Let ready-set scheduling ignore MAC/CLIP WAW edges after the final matching
  flag reader in the token stream, improving the ps2gl pure-OpenVCL aggregate
  by another 480 static and 510 estimated cycles while preserving conservative
  ordering before readers.
- Applied the same remaining-reader MAC/CLIP WAW mask to CodeGenerator
  latency-gap filling and pairing lookahead, improving the ps2gl pure-OpenVCL
  aggregate by another 54 static and 12 estimated cycles.
- Allowed `move.xyz <dst>, vf00` to use the upper-pipe zeroing form after the
  final MAC reader, matching the same MAC-liveness rule used by scheduling.
- Added behavior-preserving scheduler analysis scaffolding for basic-block
  construction and descriptor-derived dependency edges.
- Wired the scheduler analysis layer into code generation in preserve-order
  mode, establishing the production hook for ready-set scheduling.
- Added `--cost-compare` and `--cost-compare-json` for side-by-side VSM cost
  comparisons between a baseline and candidate, including signed deltas.
- Added `--cost-compare-markdown` for direct side-by-side Markdown cost tables
  suitable for renderer comparison reports.
- Added `--cost-compare-list-markdown` to read baseline/candidate VSM pairs
  from a manifest and emit a single multi-row Markdown comparison table.
- Added `--cost-compare-list-check <metric>` to fail a VSM pair manifest when
  any individual candidate exceeds its matching baseline for a selected metric.
- Markdown cost comparisons include loop-weighted static and estimated totals.
- `--cost-loop-preset ps2gl` now recognizes SCE optimized
  `EXPL_...__MAIN_LOOP` labels when weighting and comparing reference VSMs.
- The ps2gl loop preset now also maps SCE fast-family
  `adcLoop_done_lid__MAIN_LOOP` labels onto `xform_loop_lid`, so hot-loop
  comparisons weight those reference shaders correctly.
- Extended cost comparison reports with top weighted block deltas for estimated
  cycles, idle slots, and wait stalls.
- Added the first ready-set scheduler pass for straight-line arithmetic runs,
  using descriptor-derived dependencies while keeping labels, memory, waits,
  branches, and explicit barriers fixed.
- Extended the ready-set scheduler to pull plain loads earlier in straight-line
  blocks so their latency can overlap independent arithmetic.
- Added conservative adjacent upper/direct-branch pairing, preserving the
  branch delay slot and avoiding branch hoists across control flow.
- Moved the CLI version string into a shared source constant and added
  `--version` regression coverage.
- Kept alias live ranges sorted as they are built, making range intersection
  checks cheaper while preserving adjacent-range merging.
- Removed stale token-parser TODO comments now covered by instruction and
  memory descriptor metadata.
- Extended VF lifetimes inside `--LoopCS`-marked loops when register pressure
  allows, so loop temporaries that need scheduler overlap are not prematurely
  coalesced onto one physical register.
- Enabled the safe generic software-pipeline rewrite pass by default while
  keeping `--disable-generic-software-pipelining` for comparison and debugging.
- Delayed dependency-chain priority for latency-blocked ready-set candidates,
  so independent work fills Q/P latency before consumers are selected.
- Extended loop-pipeline diagnostics so every Q stage reports its loop-carried
  gap, next-producer insertion gap, deficit, and scheduling strategy.

## 0.3.3

	- Unified error-reporting into a separate class, and changed error
    display to a more standard appearance.
  - Added support for using 'cpp' as preprocessor in addition to gasp.
    If both are used, cpp will run before gasp, to stay compatible.
  - Added commandline-argument to specify alternative for cpp.
  - Added input-parsers for gasp and cpp that reflects the original
    filename and line in errorcodes to assist debugging.
  - Wrote a RPN expression-evaluator to assist pseudo-instruction LOI.
  - Added initial CLIP operands, but without proper validation.
  - Fixed MR32 so that destination-fields are rotated properly.
  - Fixed templates for RGET and RNEXT so that they generated proper
    destination fields.
  - Added a threshold for the number of times a dynamic branch can
    execute in the register allocator. This value has been initially set
    to 16, but can be changed by the commandline-parameter '--bthres'.
    This allows programs with a lot of dynamic branches that intersect
    to resolve.
  - Fixed multi-argument operands so that they can leave a trailing
    comma at the end without issuing any error.
  - If the final code-block is not terminated by a --exit/--endexit pair,
    the code-generator will now fail as it should.
  - Added commandline-argument '--version' to show current version.
  - Fixed a inconsistency in the argument-extraction.
  - Added a library of math-routines for use with LOI.

## 0.3.2

  - It's now possible to branch between code-blocks with branch-tracking
    intact. This will allow sharing subroutines between blocks.
  - Added initial support for output parameters from code-blocks.
    Currently it will only apply output parameters if the initiating
    branch for the block reaches the exit-point.
  - Ending a code-block now properly generates a termination with nop[E].
  - All currently unsupported preprocessor directives that are VCL
    specific have been filtered from code-output. (They were previously
    passed along unprocessed)
  - Fixed issue in operand-templates where an instruction that wanted a
    indirect read with an immediate offset would generate invalid code.
  - Fixed issue with destination-fields where specifying a field on the
    second (or third) argument (e.g. 'ilw VI01,0(VI00)x') argument with
    no field specified in the operand would generate an error.
  - Fixed issue involving indirect register accesses with immediates
    where GAS would complain when the immediate was omitted.
  - Added minimal extraction of memory-groups.

## 0.3.1

  - Fixed issue with branching to a subroutine from separate locations,
    which caused the branch to be aborted and ignoring any code that
    followed the BAL.

## 0.3

  - Refactored register-allocator, the new version now supports
    independent ranges for register-aliases and a more flexible
    handling of branches.
  - Reverted BAL to static branching as the register-allocator now
    can handle this case.
  - Added return-address tracking for integer-registers when using
    branches (to allow tracking branches into subroutines).
  - JR and JALR no longer aborts the current branch, but attempts
    to read the return-address and jump to it. If it isn't a valid
    address, the branch is aborted.
  - Added proper register inputs for both float & integer. You can
    now connect more than one alias to a register.
  - Added support for using I,P,Q,R and ACC as register inputs.
  - Added support for .name directive.
  - Added removal of dead code.
  - Added support for --cont tag.

## 0.2.1

  - Unified a lot of operand templates to reduce chance of error.
  - Added simplifications for SUBA, MULA, MADDA and MSUBA.
  - Fixed register allocation issue, now encloses registers that
    are reused due to branching (loops).
  - Fix for JR and JALR, they now abort the current branch when
    reached, to avoid issues with returning from subroutines.
  - Flagged BAL as dynamic to support early aborts from JR and JALR.
  - BAL and JALR lacked the write-modifier on the register that
    contains the return address, fixed.

## 0.2

  - Implemented first iteration of the dynamic branch-tracking
    register allocator.
  - Implemented .init_v?_* operands, except the range-version.
	- Added input parameter handling for entry points.
	- Added value verification for all float & integer-aliases.
	- Added value verification for direct register writes.
  - Added proper write-dependency from pseudo-instruction LOI.
  - Rewrote input-parser to remove dependencies on strtok() and
    static char buffers.
  - Fixed issues with the templates for a lot of operands.
  - Improved argument parsing in the tokenizer.
  - Corrected so that immediates do not affect the destination field.
  - Register numbers are now allowed to be variable in size.
  - Added support for C++-style comments.

## 0.1.1

  - Fixed issues with OPMULA and OPMSUB, dvp-as required that the
    generated arguments contained valid(xyz) fields.
  - Had named the '.rem_v*'-declarations incorrectly, fixed
  - Fixed win32 code to generate proper temporary filenames
  - Changed error-output to use std::cerr instead of cout, to allow
    proper use of pipes.
  - Added sourcecode-comments output support (-c).
  - Renamed 'isblank()' to 'isBlank()' to avoid issues with defines
    and gcc.
  - Made the register extraction code stricter against syntax issues
		(e.g. 'iaddiu temp1, vi00dontcare, 1' compiled without errors)

## 0.1

  - Initial release
