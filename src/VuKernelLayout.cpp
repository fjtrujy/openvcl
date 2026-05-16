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

} // namespace vcl
