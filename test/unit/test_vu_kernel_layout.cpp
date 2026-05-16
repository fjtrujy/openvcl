// Tests for VuKernelLayout: structured handoff between modulo placer and
// a future stage-aware emitter (Track 9.G step 5a).

#include "test_harness.h"

#include "../../src/VuKernelLayout.h"

#include <algorithm>

using namespace vcl;

TEST_CASE("VuKernelLayout: default-constructed state is empty")
{
    VuKernelLayout layout;
    CHECK(layout.II == 0u);
    CHECK(layout.miiStart == 0u);
    CHECK(layout.bumps == 0u);
    CHECK(layout.stageCount == 0u);
    CHECK(layout.feasible == false);
    CHECK(layout.entries.empty());
}

TEST_CASE("VuKernelLayoutEntry: defaults match a pipe=none placeholder")
{
    VuKernelLayoutEntry ent;
    CHECK(ent.nodeIndex == 0u);
    CHECK(ent.tokenIndex == 0u);
    CHECK(ent.slot == 0u);
    CHECK(ent.stage == 0u);
    CHECK(ent.modSlot == 0u);
    CHECK(ent.pipe == 0);
    CHECK(ent.duration == 1u);
}

TEST_CASE("VuKernelLayout: comparator sorts by (stage, modSlot, nodeIndex)")
{
    VuKernelLayoutEntry a;
    a.stage = 1; a.modSlot = 5; a.nodeIndex = 7;
    VuKernelLayoutEntry b;
    b.stage = 0; b.modSlot = 9; b.nodeIndex = 0;
    VuKernelLayoutEntry c;
    c.stage = 1; c.modSlot = 2; c.nodeIndex = 4;
    VuKernelLayoutEntry d;
    d.stage = 1; d.modSlot = 5; d.nodeIndex = 1;

    std::vector<VuKernelLayoutEntry> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    v.push_back(d);
    std::sort(v.begin(), v.end(), vuKernelLayoutEntryLess);

    // b (stage 0) first.
    CHECK(v[0].stage == 0u);
    CHECK(v[0].modSlot == 9u);
    // c (stage 1, modSlot 2) next.
    CHECK(v[1].stage == 1u);
    CHECK(v[1].modSlot == 2u);
    // d (stage 1, modSlot 5, nodeIndex 1) before a (nodeIndex 7).
    CHECK(v[2].stage == 1u);
    CHECK(v[2].modSlot == 5u);
    CHECK(v[2].nodeIndex == 1u);
    CHECK(v[3].nodeIndex == 7u);
}

TEST_CASE("VuKernelLayout: II/miiStart/bumps consistent with feasible round")
{
    VuKernelLayout layout;
    layout.II         = 36;
    layout.miiStart   = 36;
    layout.bumps      = 0;
    layout.stageCount = 2;
    layout.feasible   = true;
    CHECK(layout.II == layout.miiStart + layout.bumps);
    CHECK(layout.stageCount >= 1u);
    CHECK(layout.feasible);
}

TEST_CASE("VuKernelTemplate: empty layout produces empty template")
{
    VuKernelLayout layout;
    VuKernelTemplate t;
    buildVuKernelTemplate(layout, t);
    CHECK(t.II == 0u);
    CHECK(t.slots.empty());
    CHECK(t.conflicts == 0u);
}

TEST_CASE("VuKernelTemplate: conflict-free pairing of upper and lower")
{
    VuKernelLayout layout;
    layout.II = 4;
    VuKernelLayoutEntry u0; u0.nodeIndex = 0; u0.modSlot = 0; u0.pipe = 1;
    VuKernelLayoutEntry l0; l0.nodeIndex = 1; l0.modSlot = 0; l0.pipe = 2;
    VuKernelLayoutEntry u2; u2.nodeIndex = 2; u2.modSlot = 2; u2.pipe = 1;
    VuKernelLayoutEntry f3; f3.nodeIndex = 3; f3.modSlot = 3; f3.pipe = 3;
    layout.entries.push_back(u0);
    layout.entries.push_back(l0);
    layout.entries.push_back(u2);
    layout.entries.push_back(f3);

    VuKernelTemplate t;
    buildVuKernelTemplate(layout, t);
    CHECK(t.II == 4u);
    CHECK(t.slots.size() == 4u);
    CHECK(t.conflicts == 0u);
    CHECK(t.slots[0].upper == 0);
    CHECK(t.slots[0].lower == 1);
    CHECK(t.slots[2].upper == 2);
    CHECK(t.slots[2].lower == VuKernelTemplateSlot::NO_ENTRY);
    CHECK(t.slots[3].fdiv  == 3);
    CHECK(t.slots[1].upper == VuKernelTemplateSlot::NO_ENTRY);
    CHECK(t.slots[1].lower == VuKernelTemplateSlot::NO_ENTRY);
}

TEST_CASE("VuKernelTemplate: same-lane collision at modSlot is counted")
{
    VuKernelLayout layout;
    layout.II = 2;
    VuKernelLayoutEntry a; a.nodeIndex = 0; a.modSlot = 1; a.pipe = 1;
    VuKernelLayoutEntry b; b.nodeIndex = 1; b.modSlot = 1; b.pipe = 1;
    layout.entries.push_back(a);
    layout.entries.push_back(b);

    VuKernelTemplate t;
    buildVuKernelTemplate(layout, t);
    CHECK(t.II == 2u);
    CHECK(t.conflicts == 1u);
    // Later entry overwrites earlier (diagnostic behavior).
    CHECK(t.slots[1].upper == 1);
}

TEST_CASE("VuKernelTemplate: pipe=0 (none) entries are ignored")
{
    VuKernelLayout layout;
    layout.II = 2;
    VuKernelLayoutEntry n; n.nodeIndex = 0; n.modSlot = 0; n.pipe = 0;
    layout.entries.push_back(n);
    VuKernelTemplate t;
    buildVuKernelTemplate(layout, t);
    CHECK(t.conflicts == 0u);
    CHECK(t.slots[0].upper == VuKernelTemplateSlot::NO_ENTRY);
    CHECK(t.slots[0].lower == VuKernelTemplateSlot::NO_ENTRY);
}
