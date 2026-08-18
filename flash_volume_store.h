#pragma once

#include <cstddef>
#include <cstdint>

class FlashVolumeStore
{
public:
    static constexpr size_t VolumeSize = 4096;
    static constexpr unsigned VolumeCount = 6;

    enum class Volume : unsigned
    {
        Settings = 0,
        Scalars = 1,
        Strings = 2,
        Raw0 = 3,
        Raw1 = 4,
        Raw2 = 5
    };

    static constexpr size_t HeaderSize = 16;
    static constexpr size_t PayloadCapacity =
        VolumeSize - HeaderSize;

    static bool layoutSafe();

    static bool read(
        Volume volume,
        uint16_t expectedVersion,
        void* output,
        size_t outputCapacity,
        size_t& outputLength
    );

    static bool write(
        Volume volume,
        uint16_t version,
        const void* data,
        size_t length
    );

    static bool erase(Volume volume);

    static uint32_t flashOffset(Volume volume);

    static const uint8_t* mappedData(Volume volume);

    static uint8_t* scratchBuffer();

    static bool writeRawVolume(
        Volume volume,
        const uint8_t* sectorData
    );

private:
    struct Header
    {
        uint32_t magic;
        uint16_t version;
        uint16_t length;
        uint32_t sequence;
        uint32_t crc;
    };

    static_assert(
        sizeof(Header) == HeaderSize,
        "Flash volume header must remain 16 bytes"
    );

    static constexpr uint32_t Magic =
        0x314F5641u;

    alignas(4)
    static uint8_t scratch_[VolumeSize];

    static uint32_t crc32(
        const uint8_t* data,
        size_t length,
        uint32_t crc = 0xffffffffu
    );

    static uint32_t recordCrc(
        const Header& header,
        const uint8_t* payload
    );

    static bool validVolume(Volume volume);
};
