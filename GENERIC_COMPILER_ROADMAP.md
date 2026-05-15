# OpenVCL Generic Compiler Roadmap

## Goal
Keep OpenVCL a general VCL-to-VSM compiler. The current ps2gl-shaped software pipeline emitters are useful performance references, but they must not become the compiler architecture. Any valid VCL should compile through the generic path; known-loop emitters are transitional optimizations until the generic scheduler and software pipeliner can match or beat them.

**Objective:** Reach performance parity with the original SCEI VCL compiler on representative hot loops. All performance and visual comparisons MUST use the SCEI-generated shaders (the canonical SCEI VSM outputs) as the baseline: run the same input assets, deterministic frame/timing, and emulator configuration so that per-shader `fixed + loop*n` costs and visual output are directly comparable. Use the SCEI baseline for both cost gating and for acceptance of visual equivalence; only then retire hand-written emitters for those shaders.

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

8. **Retire Pattern Emitters Incrementally** - effectively done at the dispatcher level (with caveat)
   - Replace each hand emitter only after generic scheduling/software pipelining matches correctness and reaches equal or better loop cost.
   - Delete the bespoke emitter and keep focused regression tests.
   - Done: ps2gl-shaped emitters are no longer used by default (`m_knownLoopOptimizations = false`); the dispatcher in `tryEmitKnownLoopOptimization` short-circuits before any of the `tryEmit{FastNoLights,Fast,Scei,Ps2glPrimitiveXform,LinearXform,DirLightSpec,DirLightNoSpec,PtLightSpec,PtLightNoSpec,FinalColor}SoftwarePipelineLoop` emitters can run.
   - Caveat: the bespoke emitters are still compiled into the source tree behind the opt-in flag. They cannot be deleted yet because the generic path does not reproduce their schedule quality on the loops they used to cover. See item 9 — the gap is large, not small.

9. **Close the Hot-Loop Throughput Gap** - in progress (large gap, true generic baseline established)
   - Honest baseline (see "Generic vs SCEI per-loop gap" section below): with the default-off pattern emitters, every measured ps2gl shader is currently slower than the SCEI reference under `affine_estimated_loop_cycles` measured with `--cost-loop-preset ps2gl`. The previous "MATCH on 7 shaders" baseline was produced by a stale toolchain binary still using the hand-coded fast paths and is no longer the truth.
   - Use `affine_estimated_loop_cycles` under `--cost-loop-preset ps2gl` as the per-shader gate. Prioritize the largest absolute gaps first: `indexed` (+277), `general_pv_diff_quad` (+223), `general_pv_diff` (+191), `general` (+191), then the rest.
   - The hottest sub-blocks driving the gap are `xform_loop_lid__MAIN_LOOP` (held back by single-iteration `wait_stall=7`/`waitq_stall=7` per pass and low pairing), `dir_light_loop_lid` (`waitp_stall=27` per pass with no software pipelining), and `pt_light_loop_lid` (multi-Q latency hidden by SCEI's pipelined layout but exposed in the generic schedule).
   - Required compiler work (each iteration commits, runs unit tests, and re-measures): expand multi-Q cyclic-prefix rewrites to cover `dir_light_loop_lid`/`pt_light_loop_lid` shapes (currently blocked by the read/write memory-stream value-rotation gate), tighten generic dual-issue pairing inside `xform_loop_lid` to drain `wait_stall`/`waitq_stall`, and extend the software-pipeline dependency descriptors so `general*` and `indexed` qualify for prolog/main/drain.
   - Single-iteration generic scheduling remains the fallback for loops that still fail the safety/profitability checks.
   - Acceptance: a shader is considered closed when `affine_estimated_loop_cycles` (under `--cost-loop-preset ps2gl`) is `<=` the SCEI baseline. Pattern emitters can only be deleted once every shader they cover is closed.

### Generic vs SCEI per-loop gap (true generic baseline, default `m_knownLoopOptimizations=false`)

Measured with `/usr/local/bin/openvcl --cost <vsm> --cost-loop-preset ps2gl`, comparing `ps2gl/vu1/sce_<shader>_vcl.vsm` to `ps2gl/build-openvcl-generic-roadmap-codex/vu1/<shader>_vcl.vsm` (per-iteration `affine_estimated_loop_cycles`):

| Shader                | SCE | OpenVCL (generic) |   Δ |
|---|---:|---:|---:|
| fast_nolights         |  12 |   17 |  +5 |
| fast                  |  16 |   33 | +17 |
| scei                  |  19 |   56 | +37 |
| general_nospec        |  61 |  195 | +134 |
| general_nospec_quad   |  86 |  251 | +165 |
| general_nospec_tri    |  75 |  221 | +146 |
| general_pv_diff       |  84 |  275 | +191 |
| general_pv_diff_quad  | 113 |  336 | +223 |
| general_pv_diff_tri   |  98 |  301 | +203 |
| general               |  84 |  275 | +191 |
| general_quad          | 109 |  331 | +222 |
| general_tri           |  98 |  301 | +203 |
| indexed               |  95 |  372 | +277 |

This is the honest starting point for item 9. All later iterations on this roadmap must move these numbers down without re-enabling the hand-coded SPEC pattern emitters, and without regressing the unit-test suite or the visual references in `/Users/fjtrujy/Projects/ps2_opengl_integration/pcsx2_reference_*.png`.

## Validation Loop
- `make openvcl -j8`
- `cmake --build test/build --target openvcl_unit_tests -j8`
- `ctest --test-dir test/build --output-on-failure`
- `git diff --check`
- For scheduler-affecting phases:
  - regenerate ps2gl pure-OpenVCL VSMs;
   - compare `fixed + loop*n` and loop-weighted estimated cost against SCEI/reference per shader;
   - smoke-run representative PCSX2 examples, especially `logo.elf` and `box.elf`.

Visual reference workflow (SCEI → OpenVCL)

- Purpose: first capture reference screenshots produced with the SCEI VSM shaders, then capture the same view when using OpenVCL-generated VSM and compare the images side-by-side. Use the image diffs together with the cost reports to judge regressions or wins.

- Acquire SCEI reference images
   - Ensure SCE/reference VSMs are available (example location in this workspace: `ps2gl/vu1/sce_*_vcl.vsm`).
   - Run the chosen PCSX2 example (e.g. `logo.elf` or `box.elf`) with the SCEI VSMs in place and capture a screenshot after the relevant frame(s).
   - Manual capture: press F8 in PCSX2 to save a screenshot. Save as `pcsx2_reference_<name>.png`.

- Produce OpenVCL candidate images
   - Build OpenVCL and regenerate the ps2gl pure-OpenVCL VSMs (example commands):

```bash
cd /Users/fjtrujy/Projects/openvcl
make openvcl -j8
cd /Users/fjtrujy/Projects/ps2gl
rm -f build-openvcl-pure/vu1/*_vcl.vsm build-openvcl-pure/vu1/*.vo
PATH=/Users/fjtrujy/toolchains/ps2/ps2dev/dvp/bin:$PATH cmake --build build-openvcl-pure -j8
PATH=/Users/fjtrujy/toolchains/ps2/ps2dev/dvp/bin:$PATH ctest --test-dir build-openvcl-pure --output-on-failure
```

   - Run the same PCSX2 example with the OpenVCL-generated VSMs and capture the same frame(s). Save as `pcsx2_openvcl_<name>.png`.

- Image comparison
   - Use ImageMagick to produce a diff and a numeric metric:

```bash
# absolute-error (count of differing pixels)
magick compare -metric AE pcsx2_reference_logo.png pcsx2_openvcl_logo.png diff_ae.png

# root-mean-square (normalized) for perceptual magnitude
magick compare -metric RMSE pcsx2_reference_logo.png pcsx2_openvcl_logo.png diff_rmse.png
```

   - Interpret results: `AE == 0` means pixel-perfect; non-zero AE/RMSE should be inspected via the generated `diff_*.png`. Choose a project threshold (for example, visual noise tolerance or PR-level acceptance) and record the measured metrics alongside the cost reports.

- Optional automation notes
   - If your PCSX2 build supports automated screenshots (CLI or hotkey scripting), script the run + capture step to produce reproducible frames. Otherwise prefer manual capture to ensure identical frame timing.

- Cost reports to accompany visuals
   - For each candidate/reference pair also run OpenVCL's cost analysis and comparisons:

```bash
# Per-shader cost (text)
/path/to/openvcl --cost --cost-loop-preset ps2gl /path/to/shader_vcl.vsm

# Compare SCE/reference vs OpenVCL candidate
/path/to/openvcl --cost-compare /path/to/scei_reference.vsm /path/to/openvcl_candidate.vsm

# Produce manifest table across many pairs
/path/to/openvcl --cost-compare-list-markdown --cost-loop-preset ps2gl pairs.txt
```

- Record and store
   - Save reference images, candidate images, diffs, and cost JSON into a consistent location such as `openvcl/ci/visuals/<renderer>/` so future regressions can be tracked and (optionally) checked into a guarded branch or artifact store.

- Acceptance
   - A change passes visual validation when the chosen visual metric is within the accepted threshold and the `weighted_estimated_total_cycles` or `affine_estimated_loop_cycles` is equal-to-or-better-than the SCE/reference baseline for the targeted hot loop.

## SCEI vs OpenVCL Cost Table (auto-generated)

<!-- BEGIN_COST_TABLE -->
| Shader | Baseline (file) | baseline_we | Candidate (file) | candidate_we | Δ (candidate - baseline) | baseline_affine | candidate_affine | baseline_paired | candidate_paired | baseline_nop | candidate_nop |
|---|---|---:|---|---:|---:|---|---|---:|---:|---:|---:|
| fast_nolights | sce_fast_nolights_vcl.vsm | 139 | fast_nolights_vcl.vsm | 168 | 29 | 139 + 0n | 168 + 0n | 36 | 17 | 8 | 35 |
| fast | sce_fast_vcl.vsm | 215 | fast_vcl.vsm | 331 | 116 | 215 + 0n | 331 + 0n | 66 | 26 | 6 | 117 |
| scei | scei_vcl.vsm | 204 | scei_vcl.vsm | 360 | 156 | 204 + 0n | 360 + 0n | 72 | 20 | 10 | 147 |
| general_nospec | sce_general_nospec_vcl.vsm | 455 | general_nospec_vcl.vsm | 553 | 98 | 455 + 0n | 553 + 0n | 123 | 31 | 39 | 237 |
| general_nospec_quad | sce_general_nospec_quad_vcl.vsm | 490 | general_nospec_quad_vcl.vsm | 473 | -17 | 490 + 0n | 473 + 0n | 213 | 34 | 38 | 209 |
| general_nospec_tri | sce_general_nospec_tri_vcl.vsm | 388 | general_nospec_tri_vcl.vsm | 443 | 55 | 388 + 0n | 443 + 0n | 140 | 32 | 27 | 193 |
| general_pv_diff | sce_general_pv_diff_vcl.vsm | 719 | general_pv_diff_vcl.vsm | 650 | -69 | 719 + 0n | 650 + 0n | 195 | 44 | 66 | 244 |
| general_pv_diff_quad | sce_general_pv_diff_quad_vcl.vsm | 678 | general_pv_diff_quad_vcl.vsm | 575 | -103 | 678 + 0n | 575 + 0n | 238 | 45 | 55 | 216 |
| general_pv_diff_tri | sce_general_pv_diff_tri_vcl.vsm | 651 | general_pv_diff_tri_vcl.vsm | 540 | -111 | 651 + 0n | 540 + 0n | 213 | 43 | 54 | 199 |
| general | sce_general_vcl.vsm | 734 | general_vcl.vsm | 649 | -85 | 734 + 0n | 649 + 0n | 172 | 42 | 69 | 249 |
| general_quad | sce_general_quad_vcl.vsm | 769 | general_quad_vcl.vsm | 569 | -200 | 769 + 0n | 569 + 0n | 262 | 44 | 68 | 221 |
| general_tri | sce_general_tri_vcl.vsm | 665 | general_tri_vcl.vsm | 539 | -126 | 665 + 0n | 539 + 0n | 191 | 42 | 57 | 205 |
| indexed | sce_indexed_vcl.vsm | 713 | indexed_vcl.vsm | 516 | -197 | 713 + 0n | 516 + 0n | 205 | 30 | 60 | 220 |

<!-- END_COST_TABLE -->
