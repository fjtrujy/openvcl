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

The report includes:

- static scheduled cycles;
- estimated cycles including modeled FDIV/EFU producer issue stalls and
  explicit `waitq`/`waitp` stalls;
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
- static cost reporting used to compare OpenVCL output with SCE/reference VSM.

The current refactor is consolidating instruction facts into one canonical VU
instruction metadata table. `src/VuInstructionInfo.*` now feeds parser operand
construction and cost-analysis opcode classification. The scheduler should
migrate onto the same table so resource and barrier rules are not duplicated
across the codebase.

See [TODO.md](TODO.md) for the active roadmap.

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
