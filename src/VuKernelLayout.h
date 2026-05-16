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

} // namespace vcl

#endif
