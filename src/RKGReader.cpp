#include "RKGReader.hpp"

#include <array>
#include <cstring>

// YAZ1 decompression code was translated from AtishaRibeiro
// See https://github.com/AtishaRibeiro/InputDisplay/blob/master/InputDisplay/Core/Yaz1dec.cs

static constexpr size_t HEADER_SIZE = 0x88;

RKGReader::RKGReader(uint8_t *pData)
    : m_decodedData(pData + HEADER_SIZE), m_faceIndex(0), m_dirIndex(0), m_trickIndex(0),
      m_faceDuration(0), m_dirDuration(0), m_trickDuration(0), m_frameCount(0) {
    m_compressed = !!(pData[0xC] & 0x8);
    if (m_compressed) {
        m_decodedData = YAZ1Decompress(m_decodedData);
    }

    // Swap endianness
    m_faceCount = __builtin_bswap16(*reinterpret_cast<uint16_t *>(m_decodedData));
    m_dirCount = __builtin_bswap16(*reinterpret_cast<uint16_t *>(m_decodedData + 2));
    m_trickCount = __builtin_bswap16(*reinterpret_cast<uint16_t *>(m_decodedData + 4));

    m_faceStart = m_decodedData + INPUT_HEADER_SIZE;
    m_dirStart = m_faceStart + 2 * m_faceCount;
    m_trickStart = m_dirStart + 2 * m_dirCount;
}

RKGReader::~RKGReader() {
    if (m_compressed) {
        delete[] m_decodedData;
    }
}

uint8_t *RKGReader::YAZ1Decompress(uint8_t *pData) {
    uint8_t *pRet = nullptr;
    uint32_t retLen = 0;

    // get compressed length
    uint32_t compressedLen;
    memcpy(&compressedLen, pData, sizeof(uint32_t));
    compressedLen = __builtin_bswap32(compressedLen); // Swap endianness
    pData += sizeof(uint32_t);

    int readBytes = 0;
    while (readBytes < compressedLen) {
        // Search block
        while (readBytes + 3 < compressedLen && memcmp(pData + readBytes, "Yaz1", 4) != 0) {
            readBytes++;
        }

        if (readBytes + 3 >= compressedLen) {
            return pRet;
        }

        readBytes += 4;

        // Read block size
        uint32_t blockSize;
        memcpy(&blockSize, pData + readBytes, sizeof(uint32_t));
        blockSize = __builtin_bswap32(blockSize); // Swap endianness

        uint8_t *blockDecompressed = new uint8_t[blockSize];

        // Seek past 4 byte size + 8 unused bytes
        readBytes += 12;

        readBytes += DecompressBlock(pData, readBytes, compressedLen - readBytes, blockDecompressed,
                blockSize);

        // Add to main array
        uint8_t *pRetOld = pRet;
        pRet = new uint8_t[retLen + blockSize];
        memcpy(pRet, pRetOld, retLen);
        delete[] pRetOld;
        memcpy(pRet + retLen, blockDecompressed, blockSize);
        delete[] blockDecompressed;
        retLen += blockSize;
    }

    return pRet;
}

uint16_t RKGReader::DecompressBlock(uint8_t *src, int offset, int srcSize, uint8_t *dst,
        uint32_t uncompressedSize) {
    uint16_t srcPos = 0;
    uint16_t destPos = 0;

    int validBitCount = 0; // number of valid bits left in "code" byte
    uint8_t currCodeByte = src[offset + srcPos];
    while (destPos < uncompressedSize) {
        // read new "code" byte if the current one is used upper_bound
        if (validBitCount == 0) {
            currCodeByte = src[offset + srcPos++];
            validBitCount = 8;
        }

        if ((currCodeByte & 0x80) != 0) {
            // straight copy
            dst[destPos++] = src[offset + srcPos++];
        } else {
            // RLE part
            uint8_t byte1 = src[offset + srcPos++];
            uint8_t byte2 = src[offset + srcPos++];

            int dist = ((byte1 & 0xF) << 8) | byte2;
            int copySource = destPos - (dist + 1);

            int numBytes = byte1 >> 4;
            if (numBytes == 0) {
                numBytes = src[offset + srcPos++] + 0x12;
            } else {
                numBytes += 2;
            }

            // copy run
            for (int i = 0; i < numBytes; i++) {
                dst[destPos++] = dst[copySource++];
            }
        }

        // use next bit from "code" byte
        currCodeByte <<= 1;
        validBitCount--;
    }

    return srcPos;
}

uint8_t RKGReader::CalcFace(uint32_t frame) {
    // End of input?
    if (m_faceIndex >= m_faceCount) {
        return 0;
    }

    uint8_t tupleDuration = m_faceStart[2 * m_faceIndex + 1];

    // If new frame, then update our trackers
    if (frame > m_frameCount && ++m_faceDuration == tupleDuration) {
        m_faceIndex++;
        m_faceDuration = 0;
    }

    uint8_t inputs = m_faceStart[2 * m_faceIndex];

    return inputs;
}

uint8_t RKGReader::CalcDir(uint32_t frame) {
    // End of input?
    if (m_dirIndex >= m_dirCount) {
        return 0;
    }

    uint8_t tupleDuration = m_dirStart[2 * m_dirIndex + 1];

    // If new frame, then update our trackers
    if (frame > m_frameCount && ++m_dirDuration == tupleDuration) {
        m_dirIndex++;
        m_dirDuration = 0;
    }

    uint8_t inputs = m_dirStart[2 * m_dirIndex];

    return inputs;
}

DPad RKGReader::CalcTrick(uint32_t frame) {
    // End of input?
    if (m_trickIndex >= m_trickCount) {
        return DPad::None;
    }

    uint8_t inputs = m_trickStart[2 * m_trickIndex];
    uint8_t tupleDuration = m_trickStart[2 * m_trickIndex + 1];
    // Trick tuple duration is computed differently
    // The lower 4 bits of inputs represent how many repetitions of 256 frames there are for the
    // tuple duration
    uint16_t idleDuration = (inputs & 0x0F) * 256;

    // If new frame, then update our trackers
    if (frame > m_frameCount && ++m_trickDuration == idleDuration + tupleDuration) {
        m_trickIndex++;
        m_trickDuration = 0;

        inputs = m_trickStart[2 * m_trickIndex];
        idleDuration = (inputs & 0x0F) * 256;
    }

    // Check if we are in the idle period
    if (m_trickDuration < idleDuration) {
        inputs = 0;
    }

    return static_cast<DPad>((inputs >> 4) & 0x07);
}

/// @brief Convert raw 0-14 analog stick values to their analog equivalent (keeping in mind
/// that diagonal inputs are restricted to a unit circle in-game).
uint8_t RKGReader::RawToStick(uint8_t raw) const {
    static constexpr std::array<uint8_t, 15> sticks = {{
            59,
            68,
            77,
            86,
            95,
            104,
            112,
            128,
            152,
            161,
            170,
            179,
            188,
            197,
            205,
    }};

    return sticks[raw];
}

// Declare global variables to help with input loop mode
uint16_t g_startOfLoopFrame;
uint16_t g_startOfLoopFaceIndex;
uint16_t g_startOfLoopDirIndex;
uint16_t g_startOfLoopTrickIndex;
uint16_t g_startOfLoopFaceDuration;
uint16_t g_startOfLoopDirDuration;
uint16_t g_startOfLoopTrickDuration;

GCPadStatus RKGReader::CalcFrame(uint32_t frame) {
    constexpr bool CTGP_TAS_REPLAY_MODE = false;
    constexpr bool INPUT_LOOP_MODE = true;
    uint16_t FRAMES_AFTER_RECONNECT = 284; // This can vary, adjust this if needed

    GCPadStatus ret = s_defaultGCPadStatus;

    if (INPUT_LOOP_MODE) {
        constexpr uint16_t START_OF_LOOP = 1;
        constexpr uint16_t END_OF_LOOP = 1804;
        constexpr uint16_t DURATION_OF_LOOP = END_OF_LOOP - START_OF_LOOP;

        if (frame == 0) {
            ret.a = 1;
            return ret;
        }
        else if (frame < FRAMES_AFTER_RECONNECT) {
            return ret;
        }
        else if (frame == START_OF_LOOP + FRAMES_AFTER_RECONNECT) {
            // Keep record of every relevant variable to restore this state when we loop
            g_startOfLoopFrame = m_frameCount;
            g_startOfLoopDirIndex = m_dirIndex;
            g_startOfLoopFaceIndex = m_faceIndex;
            g_startOfLoopTrickIndex = m_trickIndex;
            g_startOfLoopDirDuration = m_dirDuration;
            g_startOfLoopFaceDuration = m_faceDuration;
            g_startOfLoopTrickDuration = m_trickDuration;
        }
        else if (frame >= END_OF_LOOP + FRAMES_AFTER_RECONNECT)
        {
            // Keep frame in the range of the input loop
            frame -= FRAMES_AFTER_RECONNECT;
            frame -= START_OF_LOOP;
            frame %= DURATION_OF_LOOP;
            frame += FRAMES_AFTER_RECONNECT;
            frame += START_OF_LOOP;

            if (frame == START_OF_LOOP + FRAMES_AFTER_RECONNECT)
            {
                // Reset to the state of the first frame of the loop, successfully looping
                m_frameCount = g_startOfLoopFrame;
                m_dirIndex = g_startOfLoopDirIndex;
                m_faceIndex = g_startOfLoopFaceIndex;
                m_trickIndex = g_startOfLoopTrickIndex;
                m_dirDuration = g_startOfLoopDirDuration;
                m_faceDuration = g_startOfLoopFaceDuration;
                m_trickDuration = g_startOfLoopTrickDuration;
                frame = m_frameCount;
            }
        }
    }

    else if (CTGP_TAS_REPLAY_MODE) {
        constexpr uint16_t FRAME_POPUP_CTGP = 241;
        constexpr uint16_t FRAMES_END_ACTIVATION = 200;    // Frames to spam left/right to activate TAS replay mode
        FRAMES_AFTER_RECONNECT = FRAMES_END_ACTIVATION + FRAME_POPUP_CTGP + 1;

        // Press A when controller connected
        if (frame == 0) {
            ret.a = 1;
            return ret;
        }

        // Spam left/right on the DPad every other frame for a full second to activate CTGP's "TAS replay" mode
        else if (frame % 2 == 1 && frame < FRAMES_END_ACTIVATION) {
            ret.dLeft = 1;
            return ret;
        }
        else if (frame % 2 == 0 && frame < FRAMES_END_ACTIVATION) {
            ret.dRight = 1;
            return ret;
        }

        // End DPad spam after triggering the TAS replay mode
        else if (frame == FRAMES_END_ACTIVATION) {
            ret.a = 1;
            return ret;
        }

        // Wait the rest of the countdown
        else if (frame < FRAMES_AFTER_RECONNECT) {
            return ret;
        }
    }

    else {
        // Factor in controller disconnection screen A press + fade out
        if (frame == 0) {
            ret.a = 1;
            return ret;
        } else if (frame < FRAMES_AFTER_RECONNECT) {
            return ret;
        }
    }

    frame -= FRAMES_AFTER_RECONNECT;

    uint8_t faceData = CalcFace(frame);
    uint8_t dirData = CalcDir(frame);
    DPad trickData = CalcTrick(frame);

    ret.a = !!(faceData & 0x01);
    ret.b = !!(faceData & 0x02);
    ret.l = !!(faceData & 0x04);

    ret.xStick = RawToStick(dirData >> 4 & 0x0F);
    ret.yStick = RawToStick(dirData & 0x0F);

    ret.dUp = trickData == DPad::Up;
    ret.dDown = trickData == DPad::Down;
    ret.dLeft = trickData == DPad::Left;
    ret.dRight = trickData == DPad::Right;

    // If new frame, update frame count
    if (frame > m_frameCount) {
        m_frameCount++;
    }

    return ret;
}
