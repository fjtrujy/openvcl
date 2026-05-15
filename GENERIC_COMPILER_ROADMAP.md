# OpenVCL Generic Compiler Roadmap

## Goal
Keep OpenVCL a general VCL-to-VSM compiler. The current ps2gl-shaped software pipeline emitters are useful performance references, but they must not become the compiler architecture. Any valid VCL should compile through the generic path; known-loop emitters are transitional optimizations until the generic scheduler and software pipeliner can match or beat them.

## Principles
- Generic correctness comes first: valid VCL must produce valid VSM without relying on ps2gl-specific pattern emitters.
- Hand-written emitters are benchmarks/oracles, not the long-term implementation.
- Generated VSM may only change during explicit scheduler or software-pipelining phases.
- Performance is measured per shader and per loop as `fixed + loop*n`, not only as a global total.

## Execution Plan

1. **Classify Codegen Paths** - done
   - Separate generic emission, generic scheduling helpers, and known-loop optimizations.
   - Done: add a switch to disable known-loop optimizations so the generic compiler path can be tested directly.
   - Done: generic compilation is now the default; known-loop emitters are opt-in reference paths via `--enable-known-loop-optimizations`.
   - Done: document known-loop emitters as transitional ps2gl-oriented optimizations in the code wrapper.

2. **Protect Generic Correctness** - done
   - Done: add tests that compile every current software-pipeline fixture through the default generic path.
   - Done: verify VSM is still produced, original labels remain, and optimized `__MAIN_LOOP` labels are absent.

3. **Use Hand Emitters as Performance References** - done
   - Keep current optimized emitters while the generic scheduler matures.
   - Compare their output and cost against the generic path and SCEI references.
   - Done: integration tests now emit optimized and generic fast_nolights VSM, then compare loop-cost JSON so the hand emitter is a measurable reference.

4. **Build Generic Basic-Block Scheduling** - done
   - Split token streams by labels, branches, barriers, continuations, and memory/control boundaries.
   - Build dependency graphs from `VuTokenResourceAccess`.
   - Initially emit the same order to validate structure.
   - Done: basic blocks now preserve explicit terminator kind and token pointer for branches, xgkick, compiler boundaries, and preordered barriers.
   - Done: dependency graphs are descriptor-backed and cover explicit VF/VI registers, implicit resources, memory ordering, and barriers.

5. **Implement Generic Dual-Pipe Scheduling** - started
   - Schedule upper/lower pipe instructions from a ready set.
   - Respect latency, Q/P waits, I, ACC, MAC, CLIP, memory, branch delays, and bypass metadata.
   - Improve cost while keeping output valid for arbitrary VCL.
   - Done: ready-set scheduling now keeps independent opposite-pipe ready instructions adjacent so the generic emitter can dual-issue them.
   - Done: scheduler analysis exposes explicit issue slots with first/second and upper/lower token pointers for future generic bundling.
   - Done: `--dump-schedule-info` and `--dump-schedule-info-json` expose those issue slots for tooling and tests.
   - Done: ready-scheduler issue-slot pairs are now tagged in the scheduled token stream and honored by codegen before the older lookahead pairing fallback.
   - Done: ready-set scheduling now tracks register/Q/P/MAC/CLIP readiness while choosing from the ready set, so independent work is preferred over consumers that would immediately need latency padding.
   - Done: dependency-chain priority is ignored while a ready-set candidate is still latency-blocked, so independent ready work can fill Q/P gaps before consumers issue.
   - Done: code emission and scheduler analysis now share one `VuLatencyTracker` for readiness and bypass math instead of maintaining duplicate hazard models.
   - Done: schedule dumps now apply the same generic software-pipeline rewrite as codegen before showing ready-set issue slots.
   - Done: schedule dumps now mark explicit branch-delay filler tokens, making delay-slot rewrite decisions visible in text and JSON.
   - Done: ready-scheduler issue-slot dumps now include explicit latency-padding slots, exposing idle cycles that code emission would otherwise insert later.
   - Done: latency-padding slots now identify `nop`, `waitq`, and `waitp` padding kinds, matching the emitter's long-latency wait choices in schedule tooling.
   - Done: issue slots now expose modeled issue cycles and cycle counts, so multi-cycle `waitq`/`waitp` spans are visible to future generic emitters.
   - Done: code emission and scheduler analysis now share one helper for selecting NOP vs `waitq`/`waitp` hazard padding.
   - Done: schedule issue-slot dumps now use the same MAC/CLIP flag-liveness segmentation as generic code emission.
   - Done: code emission and scheduler analysis now share the helper for deciding when remaining MAC/CLIP WAW dependencies are dead.
   - Done: schedule issue-slot dumps now expose the ignored implicit WAW resources for each scheduler segment.
   - Done: ready-scheduler analysis now exposes a typed scheduled-program wrapper with block-level cycle ranges and program-relative issue cycles.
   - Done: ready-set token scheduling now flattens the typed scheduled-program wrapper, keeping generic codegen and scheduler dumps on the same plan.
   - Done: scheduled issue slots now store their first/second/upper/lower token indices directly, making the typed schedule model self-contained for future direct emission.
   - Done: the non-flag ready-set API now also routes through a typed scheduled-program wrapper instead of maintaining a separate token-only scheduler path.
   - Done: `--strict-schedule-slots` can now compile using scheduler-selected pairs while skipping legacy textual latency/pairing lookahead fallbacks, giving the generic scheduler a direct validation mode before it becomes the default.
   - Done: ready-scheduler issue slots now pair safe upper-pipe tails with following direct branches and with `xgkick`, reducing the amount of pairing legality that exists only in legacy codegen lookahead.
   - Done: strict schedule-slot emission now preserves explicit branch-delay fillers after scheduled upper+branch pairs, so direct scheduler emission can model loop-tail branch pairs without losing the delay-slot instruction.
   - Done: typed schedule-program dumps now carry Q/P/register latency across label and basic-block boundaries, exposing the padding that direct emission must eventually consume.
   - Done: strict schedule-slot emission now consumes the typed scheduled-program model directly, including explicit `nop`, `waitq`, and `waitp` padding slots across label/basic-block boundaries.
   - Done: strict schedule-slot emission now tracks terminal unconditional branches, avoiding unreachable generated exit footers in the direct typed-emission path.
   - Done: strict schedule-slot validation against ps2gl/PCSX2 exposed ordinary `lq` load-use latency as one cycle longer than the previous model; the shared latency table now pads VF consumers while preserving the load-to-`ftoi` bypass.
   - Done: generic codegen now emits directly from the typed scheduler by default; the older textual lookahead emitter remains only behind opt-in known-loop reference optimizations.
   - Done: generic scheduling can now treat dead-MAC `move.xyz` copies as upper-pipe `max.xyz` moves, making tail copies pairable without ps2gl-specific pattern emitters.

6. **Implement Generic Loop Analysis** - done
   - Detect induction registers, loop-carried dependencies, loads/stores, branch targets, and loop bodies.
   - Report cost by label and as `fixed + loop*n`.
   - Emit machine-readable JSON for comparisons.
   - Done: loop-pipeline analysis now reports memory load/store counts, pre/post-increment memory use, xgkick presence, induction registers, and read/write loop-carried registers in text and JSON.
   - Done: loop-pipeline analysis reports Q latency strategy, blockers, rotated registers, rewrite plans, and machine-readable token ranges for future modulo scheduling.
   - Done: loop-pipeline analysis now reports every Q producer token, not only the last one, so multi-Q loops can be diagnosed and scheduled explicitly.
   - Done: loop-pipeline analysis now groups each Q producer with the Q consumers it feeds, exposing per-stage latency gaps for future multi-Q modulo scheduling.
   - Done: loop-carried Q register analysis now covers every Q stage, not only the final producer/consumer pair in multi-Q loops.
   - Done: each Q stage now reports loop-carried gap, next-producer insertion gap, deficit, and scheduling strategy in text/JSON dumps.
   - Done: top-level multi-Q loop analysis now exposes all Q consumers, not only the consumers of the final Q producer.

7. **Implement Generic Software Pipelining** - started
   - Start with simple affine loops.
   - Generate prolog/main/drain automatically.
   - Compare against current hand emitters and SCEI output per shader.
   - Done: loop analysis now exposes an automatic prolog/main/drain token plan for eligible single-Q affine loops in text and JSON.
   - Done: software-pipeline analysis now separates structural opportunities from loops that are safe to emit today, with blocker reasons such as required register rotation.
   - Done: software-pipeline plans now list the concrete VF/VI registers that require rotation before generic emission can be enabled.
   - Done: the emittable subset is now limited to single-instruction Q prefetches that do not read induction registers or perform memory access, so automatic emission has an explicit safety boundary.
   - Done: the generic software-pipeline path can rewrite that safe single-Q subset into an automatic prolog plus main loop, while blocked loops continue through ordinary generic scheduling.
   - Done: generic software-pipeline emission now supports safe multi-instruction prefetch prefixes when cloned load offsets can be adjusted from induction-update metadata without clobbering the remaining suffix.
   - Done: generic software-pipeline emission now supports the first single-consumer register-rotation case by cloning prefix work into assigned scratch VF registers and moving scratch back before the loop branch.
   - Done: generic software-pipelining is enabled by default once the rewrite has passed the safety/profitability blockers; `--disable-generic-software-pipelining` remains available for comparisons.
   - Done: generic software-pipeline emission now rejects Q live-out loops so the inserted next-iteration Q producer cannot change fallthrough semantics.
   - Done: generic software-pipeline rewrites now go through an explicit `VuSoftwarePipelineRewritePlan`, giving future rotation/drain work a typed plan instead of ad hoc token surgery.
   - Done: loop-pipeline text/JSON dumps now expose the concrete rewrite plan that will be emitted, not only the abstract opportunity.
   - Done: loop-pipeline analysis now reports the original Q producer-to-consumer gap, so future profitability checks can distinguish local Q scheduling from true cross-iteration pipelining needs.
   - Done: loop-pipeline analysis now reports Q gap deficit and loop-carried Q gap cycles, giving the future modulo scheduler an explicit latency-hiding target.
   - Done: loop-pipeline analysis now reports the actual next-Q insertion gap from the last Q consumer, preventing multi-consumer loops from looking profitable when the next Q producer would be inserted too late.
   - Done: loop-pipeline analysis classifies Q latency strategy as `local`, `loop_carried`, or `insufficient`, making the future scheduler decision explicit in text/JSON dumps.
   - Done: generic software-pipeline emission now skips loops whose local Q producer/consumer gap already hides latency, preventing structurally valid but unnecessary rewrites.
   - Done: tests cover all three Q scheduling strategy outcomes, including loops with insufficient local and loop-carried work.
   - Done: loop-pipeline safety now allows multiple self-updating induction registers, which ps2gl loops need for paired input/output pointer advancement.
   - Done: multi-instruction prefetch blockers now identify memory and induction-register reads separately, exposing the next scheduler work needed by ps2gl transform loops.
   - Done: loop-pipeline analysis now exposes register-rotation descriptors with per-register input/output fields, not only a flat rotated-register list.
   - Done: register-rotation descriptors now include assigned scratch VF registers from a generic free-register scan, preparing rotation emission without hard-coded ps2gl register choices.
   - Done: loop analysis now exposes signed induction-update descriptors with token index, mnemonic, immediate, and step, giving future prefetch rewrites the data needed to adjust memory references generically.
   - Done: pipeline plans now expose prefetch descriptors for prolog tokens, including memory base/offset, induction-register use, and computed next-iteration offsets.
   - Done: generic software-pipeline emission can now emit a simple drain for Q live-out loops whose Q producer is invariant across the loop body.
   - Done: generic register-rotation emission now supports multiple Q consumers and multiple rotated VF registers when the descriptor checks prove the suffix is safe.
   - Done: rotation requirements are now filtered to registers actually written by cloned prefetch work, so later in-loop Q consumers no longer force unnecessary rotations.
   - Done: rewrite plans now distinguish cloned prefetch placement from cloned Q-producer placement, and the simple no-prefetch/no-rotation subset can put the next Q producer in the loop branch delay slot.
   - Done: explicit branch-delay fillers are preserved as scheduling barriers, keeping typed rewrite decisions intact through ready-set scheduling.
   - Done: safe cloned-prefetch and scratch-rotation rewrites can now place the next Q producer in the loop branch delay slot while leaving safe ordinary suffix work before the branch.
   - Done: rewrite-plan dumps expose the selected next-Q insertion point and whether it occupies the branch delay slot.
   - Done: generic software-pipeline rewrites now prefer safe next-Q branch-delay placement over using the branch delay for ordinary suffix work, improving the generic modulo-scheduling shape without ps2gl-specific pattern emitters.
   - Done: rewrite-plan dumps now report the suffix dependency blocker when a cloned next-Q producer cannot safely move into the branch delay slot.
   - Done: generic software-pipelined loops can now try moving trailing store-base induction updates before their stores while rewriting store offsets; codegen keeps the normalized form only when the typed scheduler reports no cycle-count regression.
   - Done: loop-pipeline text/JSON dumps now expose suffix store descriptors, including induction base, next-iteration offset, and drain-candidate status for the future modulo drain/prolog rewrite.
   - Done: suffix store descriptors now also expose the stored value register and vector fields, giving the future generic drain rewrite enough value metadata without rereading raw token text.
   - Done: concrete software-pipeline rewrite plans now carry suffix store descriptors through to text/JSON dumps, so the future emitter can consume typed store-drain metadata directly.
   - Done: software-pipeline rewrite application now uses one shared helper for safe store-base advancement, keeping codegen and scheduler dumps on the same transformed token stream.
   - Done: generic software-pipeline rewrites can now rotate suffix-store source values into scratch VF registers before cloned prefetches overwrite them, then rewrite the store to use the scratch value.
   - Done: generic software-pipeline rewrites can now emit true delayed suffix-store drains by computing the first iteration in the prolog, storing previous-iteration values at the top of the main loop, and draining the final stored values on loop exit.
   - Done: multi-Q loop analysis now exposes a dedicated cyclic-prefix plan with text/JSON blockers for unsafe prefix side effects, suffix clobbers, branch clobbers, and Q live-out cases.
   - Done: generic software-pipeline rewrites can now emit the first multi-Q cyclic-prefix subset by priming the first Q stage in a prolog, running the suffix in the main loop, and cloning that first-stage prefix before the branch.
   - Done: generic multi-Q cyclic-prefix rewrites can now rotate through the latest safe Q stage that still leaves a real loop suffix, allowing multiple Q stages to be primed and cloned without a ps2gl-specific emitter.
   - Done: multi-Q cyclic-prefix planning can now split before the first Q consumer when the first Q producer already has enough independent producer-side gap, rotating only producer-side work instead of duplicating consumed Q work.
   - Done: multi-Q cyclic-prefix planning now evaluates safe split candidates with the shared scheduler model and keeps the lowest-cycle main-loop shape instead of hard-coding first-stage or latest-stage selection.
   - Done: multi-Q cyclic-prefix emission can now insert the cloned prefix before independent loop-tail work, giving next-iteration Q producers/consumers real tail cycles to hide latency instead of forcing the clone immediately before the branch.
   - Done: software-pipeline rewrites now support labels carried by `--LoopCS` directives, which lets ps2gl-style `label: --LoopCS ...` transform loops enter the generic prolog/main/drain path.
   - Done: multi-Q cyclic-prefix planning can now prime producer-only first-stage prefixes whenever the loop-carried insertion gap is sufficient, enabling the first generic modulo shape for quad transform loops without duplicating the first Q consumer.
   - Done: cyclic-prefix planning now has scratch-rotation metadata and emission support for cloned next-iteration value prefixes, and synthetic rotation moves preserve alias registers instead of collapsing them to `VF00`.
   - Done: dependency graphs now model MAC/CLIP flag readers against the final live flag writer instead of serializing every overwritten flag writer, allowing the generic scheduler to hoist later transform slices across waiting Q consumers when the values are independently allocated.
   - Done: register allocation now has a guarded multi-Q live-range extension for Q-stage aliases, giving small generic modulo-scheduling loops distinct value registers without applying a ps2gl-specific emitter.
   - Done: cyclic-prefix clones can adjust induction-based memory offsets when the clone is inserted before the pointer update that normally advances to the next iteration.
   - Done: generic no-Q counted loops can use the same cyclic-prefix scratch-rotation path, giving W-power/final-color style loops a non-pattern modulo-scheduling route.
   - Done: no-Q cyclic-prefix planning now searches safe insertion points inside the main loop, letting rotated next-iteration loads/fma work fill value-chain latency instead of always sitting at the branch.
   - Done: ACC dependency graphs now track the last live ACC writer before ACC readers, reducing false cross-chain serialization while preserving accumulator read/write order.
   - Done: no-Q cyclic-prefix rewrites now support single-store `--LoopExtra` loops while keeping the suffix store inside the loop instead of letting legacy branch-delay passes move it to fallthrough.
   - Done: no-Q cyclic-prefix selection now prefers a store-at-top value-rotation shape when it is cost-neutral, moving final-color conversion work into the prolog/cyclic prefix.
   - Done: the shared latency model now includes the narrow SCE-observed `lq`/`lqi`/`lqd` to `minii` bypass, improving modulo-scheduled final-color/store-drain loops without relaxing ordinary load-to-FMAC padding.
   - Done: generic multi-Q cyclic-prefix loops can now emit delayed suffix-store drains for no-load/write-only counted loops; ps2gl transform/light loops remain gated until read/write memory-stream value rotation is modeled safely.
   - Done: delayed suffix-store offset calculation now uses the aggregate per-iteration induction step, so loops with multiple pointer increments model their drain addresses correctly.
   - Done: suffix-store value dependency keys now preserve vector fields for float aliases, so future delayed stores can reason about alias-backed `sq` values the same way as physical `VFxx` registers.
   - Done: loaded multi-Q suffix-store drains now keep same-base read/write memory streams blocked with an explicit `read_write_memory_stream_conflict` reason.
   - Done: loaded multi-Q single-store suffix drains can emit when read/write memory streams are separated; the peeled prolog primes the same cyclic-prefix branch-delay Q producer as the main loop and the suffix-store value rotates through scratch before later loads/clobbers.
   - Done: loaded multi-Q multi-store suffix streams are blocked with `multi_store_loaded_suffix_stream` until the generic drain model can rotate every stored value across the prolog/main/drain boundary; this keeps ps2gl transform loops visually correct.

8. **Retire Pattern Emitters Incrementally** - started
   - Replace each hand emitter only after generic scheduling/software pipelining matches correctness and reaches equal or better loop cost.
   - Delete the bespoke emitter and keep focused regression tests.
   - Done: ps2gl-shaped emitters are no longer used by default, keeping generic emission as the normal compiler path while retaining opt-in reference emitters for comparison.

9. **Close the Hot-Loop Throughput Gap** - next
   - Use loop-weighted estimated cost, not just total static cycles, as the gate for remaining performance work.
   - Prioritize the hottest ps2gl loops first: `fast_nolights`, `fast`, `scei`, then the `general*` and `indexed` families where `xform_loop_lid` and `pt_light_vert_loop_lid` dominate the weighted reports.
   - Reduce `nop_only_cycles` and raise paired cycles in the steady-state loop body by moving safe next-iteration work, especially Q producers, loads, and independent lower-pipe instructions, across the current iteration bubbles.
   - Expand the generic software-pipeline rewrite so prolog/main/epilog construction is the default loop shape whenever the dependency descriptors prove it is safe.
   - Keep the single-iteration scheduler as the fallback path for loops that still fail the safety or profitability checks.
   - Add regression coverage for affine loop cost comparisons so each hot shader can be checked against its SCE/reference counterpart by `base + loop*n`.

## Validation Loop
- `make openvcl -j8`
- `cmake --build test/build --target openvcl_unit_tests -j8`
- `ctest --test-dir test/build --output-on-failure`
- `git diff --check`
- For scheduler-affecting phases:
  - regenerate ps2gl pure-OpenVCL VSMs;
  - compare `fixed + loop*n` and loop-weighted estimated cost against SCEI/reference per shader;
  - smoke-run representative PCSX2 examples, especially `logo.elf` and `box.elf`. You can take screenshot with F8 and check the content is still correct.
