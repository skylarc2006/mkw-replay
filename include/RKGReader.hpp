#pragma once

#include "GCPadStatus.hpp"

#include <cstddef>

static constexpr size_t INPUT_HEADER_SIZE = 0x8;

enum class DPad {
    None = 0,
    Up = 1,
    Down = 2,
    Left = 3,
    Right = 4,
};

class RKGReader {
public:
    RKGReader(uint8_t *pData);
    ~RKGReader();

    GCPadStatus CalcFrame(uint32_t frame);

private:
    uint8_t *YAZ1Decompress(uint8_t *pData);
    uint16_t DecompressBlock(uint8_t *src, int offset, int srcSize, uint8_t *dst,
            uint32_t uncompressedSize);

    uint8_t CalcFace(uint32_t frame);
    uint8_t CalcDir(uint32_t frame);
    uint8_t RawToStick(uint8_t raw) const;
    DPad CalcTrick(uint32_t frame);

    // Current computed frame (since sometimes we will poll the same frame multiple times)
    uint32_t m_frameCount;

    // The number of tuples in each data section
    uint32_t m_faceCount;
    uint32_t m_dirCount;
    uint32_t m_trickCount;

    // Track the start of each data section so that we don't recompute every input poll
    uint8_t *m_faceStart;
    uint8_t *m_dirStart;
    uint8_t *m_trickStart;

    // What is the current tuple we're looking at?
    uint32_t m_faceIndex;
    uint32_t m_dirIndex;
    uint32_t m_trickIndex;

    // If a given tuple is x frames long, how many frames have we elapsed in this tuple so far?
    uint32_t m_faceDuration;
    uint32_t m_dirDuration;
    uint32_t m_trickDuration; // may be greater than 256

    uint8_t *m_decodedData;
    bool m_compressed;
};
