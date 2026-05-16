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

#include <string>
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
    unsigned int conflicts;
    std::vector< unsigned int > prologueTokenCounts; // size = stageCount-1
    std::vector< unsigned int > epilogueTokenCounts; // size = stageCount-1
    // Per-row VLIW slot tables. Each vector is sized (stageCount-1)*II.
    // Row index for prologue copy p (0..stageCount-2), modSlot c is p*II+c.
    // Row index for epilogue copy q (1..stageCount-1), modSlot c is (q-1)*II+c.
    std::vector< VuKernelTemplateSlot > prologueRows;
    std::vector< VuKernelTemplateSlot > epilogueRows;

    VuKernelEnvelope()
        : II( 0 ), stageCount( 0 ), kernelTokens( 0 ),
          prologueCycles( 0 ), epilogueCycles( 0 ), conflicts( 0 ) {}
};

void buildVuKernelEnvelope( const VuKernelLayout& layout,
                            VuKernelEnvelope& envelope );

// Track 9.G step 5e — register-reuse / write-generation analysis.
//
// In modulo scheduling, two iterations from adjacent stages are active
// concurrently in the steady state. If a layout entry at stage sA and
// another at stage sB (sA != sB) touch the same architectural register
// and at least one of them is a write, then those iterations alias on
// that register — the register must be renamed (or the schedule
// adjusted) before emission. This analyzer flags such hazards.

struct VuKernelEntryRegisters
{
    std::vector< std::string > reads;
    std::vector< std::string > writes;
};

struct VuKernelRegisterHazard
{
    std::string  reg;
    unsigned int entryA;
    unsigned int entryB;
    unsigned int stageA;
    unsigned int stageB;
    int          kindA; // 0 = read, 1 = write
    int          kindB;

    VuKernelRegisterHazard()
        : entryA( 0 ), entryB( 0 ), stageA( 0 ), stageB( 0 ),
          kindA( 0 ), kindB( 0 ) {}
};

struct VuKernelRegisterPlan
{
    unsigned int regCount; // distinct registers referenced by layout
    unsigned int wawCount; // write-after-write cross-stage hazards
    unsigned int rawCount; // read-after-write cross-stage hazards
    unsigned int warCount; // write-after-read cross-stage hazards
    std::vector< VuKernelRegisterHazard > hazards;

    VuKernelRegisterPlan()
        : regCount( 0 ), wawCount( 0 ), rawCount( 0 ), warCount( 0 ) {}
};

void buildVuKernelRegisterPlan(
    const VuKernelLayout& layout,
    const std::vector< VuKernelEntryRegisters >& entryRegs,
    VuKernelRegisterPlan& plan );

// Track 9.G step 6a — rewrite-plan synthesis (diagnostic).
//
// Reformulates the kernel template + pipeline envelope as flat,
// per-cycle, per-lane token-index sequences in emission order. Each
// cycle contributes four lane slots in fixed order
// (upper, lower, fdiv, efu); empty lanes carry the sentinel NO_TOKEN.
//
// Layout:
//   prologTokens.size() == (stageCount - 1) * II * 4
//   mainTokens.size()   == II * 4
//   drainTokens.size()  == (stageCount - 1) * II * 4
//
// For prolog copy p (0 <= p < stageCount-1), the lane group for modSlot
// c starts at offset (p*II + c) * 4. For drain copy q (1 <= q <= stageCount-1),
// the lane group starts at ((q-1)*II + c) * 4. Diagnostic-only: emission
// still flows through buildVuSoftwarePipelineRewritePlans.

struct VuKernelRewritePlan
{
    static const unsigned int NO_TOKEN = static_cast< unsigned int >( -1 );

    unsigned int II;
    unsigned int stageCount;
    unsigned int conflicts;
    std::vector< unsigned int > prologTokens;
    std::vector< unsigned int > mainTokens;
    std::vector< unsigned int > drainTokens;

    // Track 9.G step 6b — structured per-entry / per-stage views.
    //
    // entryStages[e] is the stage of layout.entries[e]. Convenience flat
    // view used by emission and by stage-aware rename analysis.
    //
    // stageCells is a (stage, modSlot) grid of size stageCount * II,
    // indexed (stage * II + modSlot). Each cell holds up to four lane
    // entry-indices into layout.entries (upper, lower, fdiv, efu);
    // VuKernelTemplateSlot::NO_ENTRY denotes an empty lane.
    std::vector< unsigned int >         entryStages;
    std::vector< VuKernelTemplateSlot > stageCells;

    VuKernelRewritePlan()
        : II( 0 ), stageCount( 0 ), conflicts( 0 ) {}
};

void buildVuKernelRewritePlan( const VuKernelLayout& layout,
                               const VuKernelEnvelope& envelope,
                               VuKernelRewritePlan& plan );

} // namespace vcl

#endif
