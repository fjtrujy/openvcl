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

} // namespace vcl
