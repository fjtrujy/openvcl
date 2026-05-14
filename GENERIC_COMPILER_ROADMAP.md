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

6. **Implement Generic Loop Analysis** - done
   - Detect induction registers, loop-carried dependencies, loads/stores, branch targets, and loop bodies.
   - Report cost by label and as `fixed + loop*n`.
   - Emit machine-readable JSON for comparisons.
   - Done: loop-pipeline analysis now reports memory load/store counts, pre/post-increment memory use, xgkick presence, induction registers, and read/write loop-carried registers in text and JSON.
   - Done: loop-pipeline analysis reports Q latency strategy, blockers, rotated registers, rewrite plans, and machine-readable token ranges for future modulo scheduling.

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
   - Done: generic software-pipelining is guarded by `--enable-generic-software-pipelining` until the generic modulo scheduler can prove the rewrite is profitable.
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

8. **Retire Pattern Emitters Incrementally** - started
   - Replace each hand emitter only after generic scheduling/software pipelining matches correctness and reaches equal or better loop cost.
   - Delete the bespoke emitter and keep focused regression tests.
   - Done: ps2gl-shaped emitters are no longer used by default, keeping generic emission as the normal compiler path while retaining opt-in reference emitters for comparison.

## Validation Loop
- `make openvcl -j8`
- `cmake --build test/build --target openvcl_unit_tests -j8`
- `ctest --test-dir test/build --output-on-failure`
- `git diff --check`
- For scheduler-affecting phases:
  - regenerate ps2gl pure-OpenVCL VSMs;
  - compare `fixed + loop*n` against SCEI per shader;
  - smoke-run representative PCSX2 examples, especially `logo.elf` and `box.elf`.
