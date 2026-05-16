// VuKernelLayout: structured handoff between the modulo placer and a
// future stage-aware emitter.
//
// Track 9.G step 5a — kernel layout scaffolding.
//
// Captures the result of iterative modulo scheduling in a form the
// emitter can consume directly:
//
//   - II            : initiation interval that was satisfied
//   - miiStart      : initial MII the placer started from (II = miiStart + bumps)
//   - bumps         : how many times the placer had to bump II
//   - stageCount    : number of pipeline stages (maxStage + 1)
//   - feasible      : true iff every node found a slot
//   - entries       : (nodeIndex, tokenIndex, slot, stage, modSlot, pipe)
//                     records, sorted by (stage, modSlot, nodeIndex)
//
// Pipe enum mirrors the placer's pipeKind: 0=none, 1=Upper, 2=Lower,
// 3=FDIV, 4=EFU. Diagnostic only for now; planner / emission still own
// the canonical schedule.

#ifndef OPENVCL_VU_KERNEL_LAYOUT_H
#define OPENVCL_VU_KERNEL_LAYOUT_H

#include <vector>

namespace vcl
{

struct VuKernelLayoutEntry
{
    unsigned int nodeIndex;  // index into the per-loop token vector
    unsigned int tokenIndex; // absolute token index inside indexedTokens
    unsigned int slot;       // absolute placer slot
    unsigned int stage;      // slot / II
    unsigned int modSlot;    // slot % II
    int          pipe;       // 0=none 1=Upper 2=Lower 3=FDIV 4=EFU
    unsigned int duration;   // multi-cycle pipe occupancy (1 for Upper/Lower)

    VuKernelLayoutEntry()
        : nodeIndex( 0 ), tokenIndex( 0 ), slot( 0 ), stage( 0 ),
          modSlot( 0 ), pipe( 0 ), duration( 1 ) {}
};

struct VuKernelLayout
{
    unsigned int II;
    unsigned int miiStart;
    unsigned int bumps;
    unsigned int stageCount;
    bool         feasible;
    std::vector< VuKernelLayoutEntry > entries;

    VuKernelLayout()
        : II( 0 ), miiStart( 0 ), bumps( 0 ), stageCount( 0 ),
          feasible( false ) {}
};

// Stable comparator for sorting entries by (stage, modSlot, nodeIndex).
bool vuKernelLayoutEntryLess( const VuKernelLayoutEntry& a,
                              const VuKernelLayoutEntry& b );

// Track 9.G step 5b — stage-aware kernel template.
//
// A kernel template is the per-modulo-slot VLIW shape of the steady-state
// loop body: at each modSlot c in [0, II), at most one Upper instruction
// pairs with at most one Lower instruction, and at most one each of the
// FDIV / EFU multi-cycle pipes may be live.
//
// VuKernelTemplateSlot stores indices into VuKernelLayout::entries (or
// VuKernelTemplateSlot::NO_ENTRY for an empty lane). The template is a
// derived view: no information is added beyond what the layout already
// captured.

struct VuKernelTemplateSlot
{
    static const int NO_ENTRY = -1;
    int upper;
    int lower;
    int fdiv;
    int efu;

    VuKernelTemplateSlot()
        : upper( NO_ENTRY ), lower( NO_ENTRY ),
          fdiv( NO_ENTRY ),  efu( NO_ENTRY ) {}
};

struct VuKernelTemplate
{
    unsigned int II;
    unsigned int conflicts;
    std::vector< VuKernelTemplateSlot > slots; // size == II

    VuKernelTemplate() : II( 0 ), conflicts( 0 ) {}
};

// Build a kernel template from a layout. Pipe collisions at the same
// modSlot increment `template_.conflicts` and the later entry overwrites
// the earlier (diagnostic; planner is expected to keep this at 0).
void buildVuKernelTemplate( const VuKernelLayout& layout,
                            VuKernelTemplate& template_ );

// Track 9.G step 5c — pipeline envelope (prologue + kernel + epilogue).
//
// For a kernel with `stageCount = S` stages and initiation interval II:
//   - prologue has (S - 1) copies, each II cycles long. Copy p
//     (0-indexed, 0 <= p < S-1) issues the entries whose layout
//     stage is in [0, p] (i.e. stages already pipelined-in).
//   - kernel: II cycles; all S stages active in parallel.
//   - epilogue has (S - 1) copies, each II cycles long. Copy q
//     (1-indexed, 1 <= q <= S-1) issues the entries whose layout
//     stage is in [q, S-1] (i.e. stages still draining).
//
// VuKernelEnvelope captures only the token counts per copy; the
// kernel template (step 5b) already holds the per-modSlot slot
// assignment. Diagnostic-only — emission untouched.

struct VuKernelEnvelope
{
    unsigned int II;
    unsigned int stageCount;
    unsigned int kernelTokens;
    unsigned int prologueCycles;
    unsigned int epilogueCycles;
    std::vector< unsigned int > prologueTokenCounts; // size = stageCount-1
    std::vector< unsigned int > epilogueTokenCounts; // size = stageCount-1

    VuKernelEnvelope()
        : II( 0 ), stageCount( 0 ), kernelTokens( 0 ),
          prologueCycles( 0 ), epilogueCycles( 0 ) {}
};

void buildVuKernelEnvelope( const VuKernelLayout& layout,
                            VuKernelEnvelope& envelope );

} // namespace vcl

#endif
