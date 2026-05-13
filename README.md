# OpenVCL

OpenVCL is a free VCL preprocessor for PlayStation 2 VU programs. It reads
VCL-style source, performs register allocation and scheduling, and emits
standard VSM/DSM-style output that can be assembled by the PS2 toolchain.

The project was originally written by Jesper Svennevid and Daniel Collin.
This repository is currently being modernized around ps2gl compatibility,
correct VU scheduling, and measurable VSM cost analysis.

Francisco Javier Trujillo Mata is the current main contributor and maintainer,
recovering and extending the project after a long period without active
development.

## License

OpenVCL is licensed under AFL v2.0. See [LICENSE](LICENSE).

## Background

OpenVCL has been built from public VCL documentation and VCL source examples.
No proprietary binary has been reverse engineered.

VU Command Line is a trademark of Sony Computer Entertainment. VCL is the
abbreviated name for VU Command Line.

## Build

```sh
make openvcl
```

Install into `$PS2DEV/bin` when `PS2DEV` is set:

```sh
make install
```

Or install into another prefix:

```sh
PREFIX=/usr/local make install
```

BSD users may need to use `gmake`.

## Basic Usage

Compile VCL to VSM:

```sh
./openvcl input.vcl -o output.vsm
```

Read from stdin and write to stdout:

```sh
./openvcl < input.vcl > output.vsm
```

Run MASP as the `gasp` replacement:

```sh
./openvcl -g --gasp masp input.vcl -o output.vsm
```

Show command-line help:

```sh
./openvcl -h
```

Useful options:

| option | purpose |
|---|---|
| `-c` | emit nearly original source as comments |
| `-C` | disable code reduction |
| `-d` | emit dumb/unscheduled-style code |
| `-e` | disable generated `[E]` bits |
| `-f` | disable generated `.align` directives |
| `-g` | run `gasp` or `--gasp` before VCL processing |
| `-G` | run the C preprocessor before VCL processing |
| `-I<path>` | include path for gasp/MASP |
| `-K` | keep preprocessor temporary files |
| `-L` | globally disable loop code generation |
| `-m` | generate `.mpg` and DMA tags automatically |
| `-n` | enable new syntax |
| `-o <file>` | output filename |
| `-t <n>` | optimizer timeout |
| `-u <text>` | unique label-generation string |
| `--gasp <name>` | run a specific gasp-compatible preprocessor |
| `--cpp <name>` | run a specific C preprocessor |
| `--bthres <n>` | dynamic branch visit threshold |
| `--show-reg-alloc` | print register allocation information |
| `--cost` | analyze scheduled `.vsm` cost |
| `--cost-json` | analyze scheduled `.vsm` cost as JSON |
| `--cost-loop <label>=<n>` | weight a block by expected iterations |
| `--cost-loop-preset ps2gl` | apply known ps2gl hot-loop weights |
| `--cost-compare <baseline>` | compare scheduled `.vsm` cost against a baseline |
| `--cost-compare-json <baseline>` | compare scheduled `.vsm` cost as JSON |
| `--cost-compare-markdown <baseline>` | compare scheduled `.vsm` cost as a Markdown table |
| `--cost-compare-list-markdown` | read baseline/candidate VSM pairs and emit one Markdown table |
| `--cost-compare-list-check <metric>` | fail if any listed candidate is slower than its baseline |
| `--dump-instruction-info` | print the VU instruction metadata table |
| `--dump-instruction-info-json` | print the VU instruction metadata table as JSON |

`-M`, `-P`, and `-Z` are accepted for VCL command-line compatibility.

## VSM Cost Analysis

OpenVCL can also analyze already scheduled `.vsm` files. This works for both
OpenVCL-generated VSM and SCE/reference VSM files.

Human-readable report:

```sh
./openvcl --cost shader.vsm
```

JSON report:

```sh
./openvcl --cost-json shader.vsm
```

Weight hot blocks by expected loop iterations:

```sh
./openvcl --cost --cost-loop xform_loop_lid=100 shader.vsm
```

Apply the ps2gl 100-vertex hot-loop preset:

```sh
./openvcl --cost --cost-loop-preset ps2gl shader.vsm
```

The preset recognizes both OpenVCL labels such as `xform_loop_lid` and SCE
optimized main-loop labels such as `EXPL_..._xform_loop_lid__MAIN_LOOP`. It
also maps SCE fast-family `adcLoop_done_lid__MAIN_LOOP` labels onto
`xform_loop_lid`, so it can be used for side-by-side reference comparisons.

Compare a candidate shader against a reference shader:

```sh
./openvcl --cost-compare sce_reference.vsm openvcl_candidate.vsm
```

Emit comparison output for scripts or Markdown reports:

```sh
./openvcl --cost-compare-json sce_reference.vsm openvcl_candidate.vsm
./openvcl --cost-compare-markdown sce_reference.vsm openvcl_candidate.vsm
```

Emit one Markdown table for a set of VSM pairs:

```sh
./openvcl --cost-compare-list-markdown --cost-loop-preset ps2gl pairs.txt
```

`pairs.txt` contains whitespace-separated `baseline.vsm candidate.vsm` rows.
Blank lines and `#` comments are ignored.

Fail when any listed candidate is slower than its own baseline:

```sh
./openvcl --cost-compare-list-check weighted-estimated --cost-loop-preset ps2gl pairs.txt
```

Supported check metrics are `static`, `estimated`, `weighted-static`, and
`weighted-estimated`. This check is per row: an OpenVCL shader only passes when
that specific shader is equal to or faster than its matching SCE/reference VSM.

The report includes:

- static scheduled cycles;
- estimated cycles including modeled FDIV/EFU producer issue stalls and
  explicit `waitq`/`waitp` stalls;
- loop-weighted totals when `--cost-loop` or `--cost-loop-preset` is used;
- upper/lower slot usage, paired cycles, NOP slots, and nop-only cycles;
- per-label block costs;
- weighted hot-block, idle-slot, estimated-cost, and wait-stall rankings;
- unknown-instruction and slot-mismatch checks.

## Instruction Metadata

OpenVCL exposes its shared VU instruction table for scheduling and tooling
work. The text form is useful while inspecting opcodes:

```sh
./openvcl --dump-instruction-info
```

The JSON form is intended for scripts and regression tests:

```sh
./openvcl --dump-instruction-info-json
```

Each row includes the mnemonic, pipe, execution unit, throughput, latency,
parser operand pattern, implicit resources, memory flags, branch-delay slots,
and special bypass notes.

## Scheduler Status

OpenVCL now performs conservative VU scheduling rather than only emitting
VCL `-d`-style output. Current work includes:

- bounded upper/lower pairing lookahead;
- latency-gap filling with ready independent instructions;
- deferred `waitq`/`waitp` emission;
- Q/P, I, MAC, CLIP, ACC, VF/VI, and per-field VF dependency checks;
- safe movement around selected plain loads/stores;
- branch padding reuse when an existing pure `nop/nop` cycle is available;
- adjacent upper/direct-branch pairing while preserving branch delay slots;
- deterministic alias allocation for reproducible VSM output;
- `--LoopCS`-marked loop temporaries get conservative VF lifetime expansion
  when register pressure allows, giving the scheduler room to overlap loads;
- static cost reporting used to compare OpenVCL output with SCE/reference VSM.

The current refactor is consolidating instruction facts into one canonical VU
instruction metadata table. `src/VuInstructionInfo.*` now feeds parser operand
construction and cost-analysis opcode classification. The scheduler should
migrate onto the same table so resource and barrier rules are not duplicated
across the codebase.

Current ps2gl pure-OpenVCL aggregate cost baseline:

| metric | SCE/reference | OpenVCL | delta |
|---|---:|---:|---:|
| static scheduled cycles | 6308 | 5227 | -1081 |
| estimated cycles | 6820 | 5922 | -898 |
| ps2gl-loop weighted static cycles | 100358 | 330046 | +229688 |
| ps2gl-loop weighted estimated cycles | 100870 | 380835 | +279965 |

`estimated cycles` includes modeled FDIV/EFU producer issue stalls and
explicit `waitq`/`waitp` stalls. These are static VSM estimates, not measured
runtime per draw call. The loop-weighted rows apply `--cost-loop-preset ps2gl`
to the 13 matched ps2gl renderer pairs; they better expose the remaining
hot-loop gap caused by SCE/reference prolog/main/epilog software-pipelined
loops versus OpenVCL's current single-iteration scheduling.

This baseline uses corrected ACC dependencies for multiply-add/subtract
instructions plus conservative branch-delay filling for independent integer
instructions immediately before direct branches. The ACC rule is intentionally
more conservative than older reports that let the scheduler move
`madd*`/`msub*` instructions across ACC producer chains.

The performance target is per shader, not aggregate. A scheduler change is only
complete when every matched ps2gl OpenVCL VSM is equal to or faster than its
matching SCE/reference VSM for the selected static and estimated metrics.

## Roadmap

The next major scheduler step is a real list scheduler over descriptor-backed
basic blocks. The intended shape is:

- keep `VuInstructionInfo` as the canonical instruction table for parser,
  cost analysis, resource descriptors, and scheduler tooling;
- move remaining emission-time cycle state into an explicit scheduler plan;
- improve exact memory aliasing beyond base register and constant offset;
- add precise branch-delay-slot metadata before attempting broader branch NOP
  removal or delay-slot filling;
- optimize hot ps2gl loops for estimated block cost and dual-pipe occupancy,
  guided by `--cost-compare` and `--cost-loop-preset ps2gl`;
- eventually replace bounded textual lookahead with a ready-set list scheduler
  and dual-pipe bundler that can prepare software-pipelined loop bodies.

Scheduling changes should preserve Q/P, I, MAC, CLIP, ACC, VF/VI, broadcast
field, branch-delay, and memory-ordering correctness. Each new scheduling rule
should have a focused unit or integration test, full OpenVCL test coverage,
regenerated ps2gl pure-OpenVCL VSMs, and PCSX2 smoke coverage when generated
output can affect visible examples.

## Expression Solver

OpenVCL extends `loi` expressions with math functions. These extensions make
source incompatible with standard VCL when used, but are useful for standalone
OpenVCL projects.

| function | result |
|---|---|
| `abs(x)` | absolute value |
| `exp(x)` | exponential |
| `sin(x)`, `cos(x)`, `tan(x)` | trigonometry from radians |
| `sinh(x)`, `cosh(x)`, `tanh(x)` | hyperbolic trigonometry |
| `asin(x)`, `acos(x)`, `atan(x)`, `atan2(x, y)` | inverse trigonometry |
| `pow(x, y)` | `x` raised to `y` |
| `log(x)`, `log10(x)` | logarithms |
| `sqrt(x)` | square root |
| `pi()` | pi |

Example:

```vcl
loi sin(45 * (pi()/1.8e2))
```

Function names are case-sensitive.

## Differences From SCE VCL

- If an alias contains a register-field declaration and the argument slot does
  not support fields, OpenVCL rejects it.
- `I` may not be used as an alias for integer registers.
- Float expressions are evaluated for `loi`; GAS does not handle
  float-expression immediates.
- Old-syntax field access by suffixing aliases, for example `srcx`, is not
  supported. Prefer new syntax such as `src[x]`.
- Selecting a specific simplification branch, for example `mula d,s,t`, limits
  simplification to that operand and does not expand back to the full VCL
  simplification set.

## MASP / GASP

GNU `gasp` has been removed from newer binutils. Use MASP as a compatible
replacement by installing `masp` on your `PATH` and passing:

```sh
./openvcl -g --gasp masp input.vcl -o output.vsm
```

You may also pass a full path to `--gasp`.

## Tests

Build and run the unit/integration suite:

```sh
cmake --build test/build --target openvcl_unit_tests -j8
ctest --test-dir test/build --output-on-failure
```
