# OpenVCL Roadmap

This file tracks active OpenVCL work. Historical fixed issues belong in
[CHANGELOG.md](CHANGELOG.md) or focused regression tests.

## Current Baseline

The current ps2gl pure-OpenVCL comparison, after the broadcast scheduling
correctness fix, is:

| metric | SCE/reference | OpenVCL | delta |
|---|---:|---:|---:|
| static scheduled cycles | 6308 | 5643 | -665 |
| estimated cycles | 6820 | 6364 | -456 |

`estimated cycles` includes modeled FDIV/EFU producer issue stalls and explicit
`waitq`/`waitp` stalls. The numbers are static VSM estimates, not measured
runtime per draw call.

The remaining gap is mostly poor dual-pipe occupancy and conservative padding,
especially in transform and lighting loops.

## Next Major Direction

Create one canonical VU instruction metadata table and make scheduling, parser
operand definitions, and cost analysis consume it.

First slice complete:

- `src/VuInstructionInfo.*` defines shared mnemonic normalization, pipe/unit,
  throughput, latency, and Q/P/I/wait/branch flags.
- `VsmCostAnalyzer` consumes `VuInstructionInfo` instead of owning duplicate
  opcode sets and latency/throughput tables.
- Unit tests cover broadcast mnemonic normalization, Q/P producer costs, `mfp`,
  waits, and branch metadata.

The table should describe:

- mnemonic and instruction family;
- upper/lower pipe;
- execution unit: FMAC, FDIV, LSU, IALU, BRU, RANDU, EFU, or pseudo;
- issue throughput / minimum producer spacing;
- result latency;
- explicit reads and writes;
- implicit reads and writes: ACC, I, Q, P, R, MAC flags, CLIP flags;
- VF field masks, including broadcast-source reads;
- memory behavior: load, store, base register, constant offset, pre/post
  increment, and `xgkick` barrier behavior;
- branch and delay-slot behavior;
- special bypasses, for example `ftoi* -> mtir`;
- required wait behavior for Q/P producers and consumers.

Remaining table work:

- move the remaining emission-time cycle/latency state into an explicit
  scheduler plan when the real list scheduler starts replacing lookahead
  heuristics;
- improve exact memory aliasing beyond base register and constant offset;
- improve branch metadata beyond delay/unconditional/link/register flags when
  delay-slot filling starts.

The desired user-facing shape is an inspectable table:

```sh
openvcl --dump-instruction-info
openvcl --dump-instruction-info-json
```

## Scheduler Improvements

- Extend the ready-set scheduler beyond straight-line arithmetic and plain
  loads once stores, waits, and branch-delay filling have stronger models.
- Optimize for estimated block cost, not just static row count.
- Preserve correctness for Q/P, I, MAC, CLIP, ACC, VF/VI, broadcast fields,
  branch delay rules, and memory ordering.
- Improve dual-pipe bundling after latency-gap filling so useful lower-pipe
  work is not consumed too early as a single-slot filler.
- Improve Q/P latency scheduling by moving independent work between
  `div`/`sqrt`/`rsqrt` or EFU producers and the eventual `waitq`/`waitp`.
- Add a precise branch-delay model before attempting broader branch NOP
  removal or delay-slot filling.
- Make loop-aware reports guide optimization. Hot labels such as
  `xform_loop_lid`, `pt_light_vert_loop_lid`, and `dir_light_vert_loop_lid`
  should be weighted by realistic renderer iteration counts.

## Cost Tooling Improvements

- Refine ps2gl loop presets as runtime measurements make better defaults
  obvious.

Recently completed:

- `--cost` and `--cost-json` work on both OpenVCL-generated VSM and
  SCE/reference VSM.
- `--cost-compare`, `--cost-compare-json`, and `--cost-compare-markdown`
  provide baseline/candidate totals for renderer reports without
  hand-maintained tables.
- Instruction metadata table and JSON dump tests cover the inspectable
  `--dump-instruction-info` tooling.
- `--cost-compare` and `--cost-compare-json` now include top block deltas for:
  - `top_weighted_estimated_blocks`;
  - `top_weighted_idle_blocks`;
  - `top_weighted_wait_blocks`.
- `--cost-compare-markdown` emits a side-by-side report-table row directly
  from the same baseline/candidate summary data.
- `--cost-loop-preset ps2gl` applies the known 100-vertex hot labels used by
  ps2gl shader comparisons.
- `--cost-loop-preset ps2gl` also recognizes SCE optimized
  `EXPL_...__MAIN_LOOP` labels, making weighted SCE/OpenVCL comparisons
  meaningful without hand-written duplicate loop flags.
- Direct branches can now pair with an adjacent preceding upper-pipe
  instruction while still emitting their delay slot. Broader branch-delay
  filling remains intentionally pending.
- CodeGenerator register conflict checks now consume
  `VuTokenResourceAccess` read/write descriptors instead of duplicating
  argument-level field-mask logic.
- Stateless scheduling legality rules now live in `VuSchedulingRules`, covering
  emittable tokens, movement checks, pair resource conflicts, Q/P and flag
  predicates, branch delays, and adjacent integer-add coalescing. `CodeGenerator`
  keeps the emission cycle state.
- `VuSchedulerAnalysis` now consumes `VuSchedulingRules` for ready-scheduler
  candidate gates, block barriers, memory-ordering classification, pipe
  classification, and long-latency/load prioritization.
- The ready-set scheduler can now include dependency-safe plain stores in a
  scheduling segment, letting long-latency Q/P producers move ahead of stores
  when register, implicit-resource, and memory-ordering descriptors allow it.
- Ready-set memory dependencies now use descriptor base/offset comparisons so
  distinct plain loads and stores can move independently.
- Ready-set scheduling now prefers longer dependency chains over unrelated
  short work, starting producer chains earlier in hot renderer blocks.

## Correctness Guardrails

Every scheduling improvement should include:

- a small VCL or VSM regression test that isolates the rule;
- full OpenVCL unit/integration tests;
- regenerated ps2gl pure-OpenVCL VSMs;
- PCSX2 smoke coverage for representative ps2gl examples when the rule can
  affect visible output.

Known fragile areas:

- broadcast source fields, for example implicit `mulw ... VFw` reads;
- `loi` and FMAC instructions that read `I`;
- `clipw` and CLIP flag readers such as `fcand`;
- MAC flag readers such as `fmand`;
- branch delay slots and generated `[E]` footers;
- plain-store/load movement around VI base updates and `xgkick`.

## Smaller Cleanup Items

- Complete or remove stale inline TODO comments once the instruction metadata
  table makes their intent explicit.
- Automate `--version` updates or move version ownership into one constant.
- Store memory-group information in token arguments if it remains useful after
  the memory descriptor work.
- Keep diagnostic repros in `test/repro/` only when they still explain an
  active or historically important bug.

## Fixed Historical Items

- `.init_vf` / `.init_vi` register ranges now work end-to-end and are covered
  by `test/integration/test_init_vf_range.cpp`.
- CLIP operand validation now produces a failing exit status and is covered by
  `test/integration/test_clip_validation.cpp`.
- Loop back-edges extend pre-loop alias live ranges through loop bodies.
- `loi` expression output now emits IEEE-754 hex through the raw immediate path.
- ps2gl GL_QUADS rendering and logo lighting issues have regression coverage
  through focused scheduler tests and PCSX2 smoke checks.
