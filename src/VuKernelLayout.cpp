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
    envelope.conflicts      = 0;
    envelope.prologueTokenCounts.clear();
    envelope.epilogueTokenCounts.clear();
    envelope.prologueRows.clear();
    envelope.epilogueRows.clear();
    if( layout.stageCount < 2 || layout.II == 0 ) return;

    const unsigned int copies = layout.stageCount - 1;
    envelope.prologueCycles = copies * layout.II;
    envelope.epilogueCycles = copies * layout.II;
    envelope.prologueTokenCounts.resize( copies, 0 );
    envelope.epilogueTokenCounts.resize( copies, 0 );
    envelope.prologueRows.resize( copies * layout.II );
    envelope.epilogueRows.resize( copies * layout.II );

    for( unsigned int i = 0; i < layout.entries.size(); ++i )
    {
        const VuKernelLayoutEntry& e = layout.entries[i];
        const unsigned int st  = e.stage;
        const unsigned int ms  = e.modSlot;
        const int          pp  = e.pipe;
        if( ms >= layout.II ) continue;

        // Prologue copies p in [st, copies-1]; stages [0,p] active.
        for( unsigned int p = st; p < copies; ++p )
        {
            ++envelope.prologueTokenCounts[p];
            if( pp == 0 ) continue;
            VuKernelTemplateSlot& row = envelope.prologueRows[p * layout.II + ms];
            int* lane = NULL;
            if     ( pp == 1 ) lane = &row.upper;
            else if( pp == 2 ) lane = &row.lower;
            else if( pp == 3 ) lane = &row.fdiv;
            else if( pp == 4 ) lane = &row.efu;
            else continue;
            if( *lane != VuKernelTemplateSlot::NO_ENTRY ) ++envelope.conflicts;
            *lane = (int)i;
        }

        // Epilogue copies q in [1, min(st,copies)]; stages [q,S-1] active.
        const unsigned int qhi = ( st < copies ) ? st : copies;
        for( unsigned int q = 1; q <= qhi; ++q )
        {
            ++envelope.epilogueTokenCounts[q - 1];
            if( pp == 0 ) continue;
            VuKernelTemplateSlot& row = envelope.epilogueRows[(q - 1) * layout.II + ms];
            int* lane = NULL;
            if     ( pp == 1 ) lane = &row.upper;
            else if( pp == 2 ) lane = &row.lower;
            else if( pp == 3 ) lane = &row.fdiv;
            else if( pp == 4 ) lane = &row.efu;
            else continue;
            if( *lane != VuKernelTemplateSlot::NO_ENTRY ) ++envelope.conflicts;
            *lane = (int)i;
        }
    }
}

} // namespace vcl
