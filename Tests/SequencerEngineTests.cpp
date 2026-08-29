#include "SequencerEngine.h"
#include <iostream>

#define REQUIRE(condition) do { if (! (condition)) { \
    std::cerr << "FAILED: " #condition " at line " << __LINE__ << '\n'; return 1; \
} } while (false)

int main()
{
    using namespace pfs;
    static_assert (nodeCount == 31);
    static_assert (SequencerEngine::flatIndexFor (0, 0) == 0);
    static_assert (SequencerEngine::flatIndexFor (4, 15) == 30);
    static_assert (SequencerEngine::levelForFlatIndex (0) == 0);
    static_assert (SequencerEngine::levelForFlatIndex (30) == 4);
    static_assert (SequencerEngine::indexForFlatIndex (30) == 15);
    static_assert (SequencerEngine::levelForFlatIndex (31) == -1);

    // The state array, rendering/hit-test buttons, traversal coordinates, and
    // animation endpoints all share this exact five-row/31-node topology.
    int mappedNodes = 0;
    int mappedBranches = 0;
    for (int level = 0; level < levels; ++level)
        for (int index = 0; index < (1 << level); ++index)
        {
            const auto flat = SequencerEngine::flatIndexFor (level, index);
            REQUIRE (SequencerEngine::hasVisibleNode (level, index, flat));
            REQUIRE (SequencerEngine::levelForFlatIndex (flat) == level);
            REQUIRE (SequencerEngine::indexForFlatIndex (flat) == index);
            ++mappedNodes;

            if (level < levels - 1)
                for (int branch = 0; branch < 2; ++branch)
                {
                    const auto childIndex = index * 2 + branch;
                    const auto childFlat = SequencerEngine::flatIndexFor (level + 1, childIndex);
                    REQUIRE (SequencerEngine::hasVisibleNode (level + 1, childIndex, childFlat));
                    ++mappedBranches;
                }
        }
    REQUIRE (mappedNodes == nodeCount);
    REQUIRE (mappedBranches == nodeCount - 1);

    SequencerEngine engine (1234);
    TrackState track;
    for (auto& node : track.nodes)
        node.active = true;

    track.variation = false;
    const int expectedLevels[] { 0, 1, 2, 3, 4, 0 };
    for (int i = 0; i < 6; ++i)
    {
        const auto result = engine.step (track);
        REQUIRE (result.advanced);
        REQUIRE (result.level == expectedLevels[i]);
        REQUIRE (result.index == 0);
        REQUIRE (result.cycleStart == (i == 0 || i == 5));
    }

    track.nodes[1].midi = 72;
    engine.reset (track);
    const auto first = engine.step (track);
    REQUIRE (first.active && first.midi == rootMidi);
    REQUIRE (first.branchFrom == 0 && first.branchTo == 1);
    const auto second = engine.step (track);
    REQUIRE (second.active && second.midi == 72);
    REQUIRE (second.branchFrom == 1 && second.branchTo == 3);

    // Two variation tracks at the same rate must remain phase-locked.  Random
    // branch choices may differ, but they may never insert or lose a step.
    TrackState variationA, variationB;
    variationA.variation = variationB.variation = true;
    for (auto* variationTrack : { &variationA, &variationB })
        for (auto& node : variationTrack->nodes)
            node.active = true;
    for (int step = 0; step < 64; ++step)
    {
        const auto a = engine.step (variationA);
        const auto b = engine.step (variationB);
        REQUIRE (a.advanced && b.advanced && a.active && b.active);
        REQUIRE (a.level == b.level);
        REQUIRE (a.cycleStart == b.cycleStart);
        REQUIRE (a.cycleStart == ((step % levels) == 0));
        REQUIRE (SequencerEngine::hasVisibleNode (a.level, a.index, a.flatIndex));
        REQUIRE (SequencerEngine::hasVisibleNode (b.level, b.index, b.flatIndex));
        if (a.branchTo >= 0)
            REQUIRE (SequencerEngine::levelForFlatIndex (a.branchTo) == a.level + 1);
        else
            REQUIRE (a.level == levels - 1);
        if (b.branchTo >= 0)
            REQUIRE (SequencerEngine::levelForFlatIndex (b.branchTo) == b.level + 1);
        else
            REQUIRE (b.level == levels - 1);
    }

    // Inactive nodes are rests, never missing traversal steps.
    TrackState silentVariation;
    silentVariation.variation = true;
    for (int step = 0; step < levels * 8; ++step)
    {
        const auto visit = engine.step (silentVariation);
        REQUIRE (visit.advanced);
        REQUIRE (! visit.active);
        REQUIRE (visit.level == step % levels);
        REQUIRE (SequencerEngine::hasVisibleNode (visit.level, visit.index, visit.flatIndex));
    }

    std::cout << "PFS sequencer tests passed\n";
    return 0;
}
