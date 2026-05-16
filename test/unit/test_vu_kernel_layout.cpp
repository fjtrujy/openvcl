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
