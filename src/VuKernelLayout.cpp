#include "VuKernelLayout.h"

namespace vcl
{

bool vuKernelLayoutEntryLess( const VuKernelLayoutEntry& a,
                              const VuKernelLayoutEntry& b )
{
    if( a.stage != b.stage )       return a.stage   < b.stage;
    if( a.modSlot != b.modSlot )   return a.modSlot < b.modSlot;
    return a.nodeIndex < b.nodeIndex;
}

void buildVuKernelTemplate( const VuKernelLayout& layout,
                            VuKernelTemplate& template_ )
{
    template_.II        = layout.II;
    template_.conflicts = 0;
    template_.slots.clear();
    if( layout.II == 0 ) return;
    template_.slots.resize( layout.II );

    for( unsigned int i = 0; i < layout.entries.size(); ++i )
    {
        const VuKernelLayoutEntry& ent = layout.entries[i];
        if( ent.modSlot >= layout.II ) { ++template_.conflicts; continue; }
        VuKernelTemplateSlot& s = template_.slots[ent.modSlot];
        int* lane = NULL;
        switch( ent.pipe )
        {
        case 1: lane = &s.upper; break;
        case 2: lane = &s.lower; break;
        case 3: lane = &s.fdiv;  break;
        case 4: lane = &s.efu;   break;
        default: continue; // pipe=0 (none) is invisible to the template
        }
        if( *lane != VuKernelTemplateSlot::NO_ENTRY ) ++template_.conflicts;
        *lane = (int)i;
    }
}

void buildVuKernelEnvelope( const VuKernelLayout& layout,
                            VuKernelEnvelope& envelope )
{
    envelope.II             = layout.II;
    envelope.stageCount     = layout.stageCount;
    envelope.kernelTokens   = (unsigned int)layout.entries.size();
    envelope.prologueCycles = 0;
    envelope.epilogueCycles = 0;
    envelope.prologueTokenCounts.clear();
    envelope.epilogueTokenCounts.clear();
    if( layout.stageCount < 2 ) return;

    const unsigned int copies = layout.stageCount - 1;
    envelope.prologueCycles = copies * layout.II;
    envelope.epilogueCycles = copies * layout.II;
    envelope.prologueTokenCounts.resize( copies, 0 );
    envelope.epilogueTokenCounts.resize( copies, 0 );

    for( unsigned int i = 0; i < layout.entries.size(); ++i )
    {
        const unsigned int st = layout.entries[i].stage;
        // Prologue copy p activates stages [0, p]; entry contributes to
        // copies p in [st, copies-1].
        for( unsigned int p = st; p < copies; ++p )
            ++envelope.prologueTokenCounts[p];
        // Epilogue copy q (1..S-1) activates stages [q, S-1]; entry
        // contributes to copies q in [1, st]. Store at index q-1.
        const unsigned int qhi = ( st < copies ) ? st : copies;
        for( unsigned int q = 1; q <= qhi; ++q )
            ++envelope.epilogueTokenCounts[q - 1];
    }
}

} // namespace vcl
