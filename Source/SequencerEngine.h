#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>

namespace pfs
{
constexpr int levels = 5;
constexpr int nodeCount = (1 << levels) - 1;
constexpr int rootMidi = 60;

struct Node
{
    bool active = false;
    int midi = rootMidi;
};

struct TrackState
{
    std::array<Node, nodeCount> nodes {};
    bool variation = true;
    int rateDivisor = 1;
    int level = 0;
    int nodeIndex = 0;
};

struct StepResult
{
    int level = 0;
    int index = 0;
    int flatIndex = 0;
    int midi = rootMidi;
    bool active = false;
    bool advanced = false;
    bool cycleStart = false;
    int branchFrom = -1;
    int branchTo = -1;
};

class SequencerEngine
{
public:
    explicit SequencerEngine (std::uint32_t seed = 0x504653u) : random (seed) {}

    static constexpr int flatIndexFor (int level, int index) noexcept
    {
        return ((1 << level) - 1) + index;
    }

    static constexpr bool isValidCoordinate (int level, int index) noexcept
    {
        return level >= 0 && level < levels && index >= 0 && index < (1 << level);
    }

    static constexpr int levelForFlatIndex (int flatIndex) noexcept
    {
        if (flatIndex < 0 || flatIndex >= nodeCount)
            return -1;

        for (int level = 0; level < levels; ++level)
            if (flatIndex < flatIndexFor (level + 1, 0))
                return level;

        return -1;
    }

    static constexpr int indexForFlatIndex (int flatIndex) noexcept
    {
        const auto level = levelForFlatIndex (flatIndex);
        return level >= 0 ? flatIndex - flatIndexFor (level, 0) : -1;
    }

    static constexpr bool hasVisibleNode (int level, int index, int flatIndex) noexcept
    {
        return isValidCoordinate (level, index)
            && flatIndexFor (level, index) == flatIndex
            && flatIndex >= 0 && flatIndex < nodeCount;
    }

    void reset (TrackState& track) const noexcept
    {
        track.level = 0;
        track.nodeIndex = 0;
    }

    StepResult step (TrackState& track)
    {
        StepResult result;
        result.cycleStart = track.level == 0;
        result.level = track.level;
        result.index = track.nodeIndex;
        result.flatIndex = flatIndexFor (result.level, result.index);
        result.branchFrom = flatIndexFor (track.level, track.nodeIndex);
        const auto& node = track.nodes[static_cast<std::size_t> (result.flatIndex)];
        result.midi = node.midi;
        result.active = node.active;
        result.advanced = true;

        // Match the browser engine exactly: the current node consumes this
        // clock tick.  Random only chooses the child that will be visited on
        // the next tick.  It never searches for an active node or touches time.
        if (track.level < levels - 1)
        {
            const auto branch = track.variation ? branchChoice (random) : 0;
            track.nodeIndex = track.nodeIndex * 2 + branch;
            ++track.level;
            result.branchTo = flatIndexFor (track.level, track.nodeIndex);
        }
        else
        {
            track.level = 0;
            track.nodeIndex = 0;
            result.branchTo = -1;
        }

        return result;
    }

private:
    std::mt19937 random;
    std::uniform_int_distribution<int> branchChoice { 0, 1 };
};
}
