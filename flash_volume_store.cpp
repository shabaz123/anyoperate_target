#include "flash_volume_store.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/platform.h"

extern "C" uint8_t __flash_binary_end;

alignas(4)
uint8_t FlashVolumeStore::scratch_[FlashVolumeStore::VolumeSize];

namespace
{
    struct FlashWriteParameters
    {
        uint32_t offset;
        const uint8_t* sector;
    };

    void doWriteSector(void* parameter)
    {
        auto* p =
            static_cast<FlashWriteParameters*>(parameter);

        flash_range_erase(
            p->offset,
            FLASH_SECTOR_SIZE
        );

        flash_range_program(
            p->offset,
            p->sector,
            FLASH_SECTOR_SIZE
        );
    }

    void doEraseSector(void* parameter)
    {
        const uint32_t offset =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(parameter)
            );

        flash_range_erase(
            offset,
            FLASH_SECTOR_SIZE
        );
    }
}

bool FlashVolumeStore::validVolume(
    Volume volume
)
{
    return static_cast<unsigned>(volume) <
           VolumeCount;
}

uint32_t FlashVolumeStore::flashOffset(
    Volume volume
)
{
    const unsigned index =
        static_cast<unsigned>(volume);

    return
        PICO_FLASH_SIZE_BYTES -
        ((index + 1u) * VolumeSize);
}

const uint8_t* FlashVolumeStore::mappedData(
    Volume volume
)
{
    if (!validVolume(volume))
    {
        return nullptr;
    }

    return reinterpret_cast<const uint8_t*>(
        XIP_BASE + flashOffset(volume)
    );
}

uint8_t* FlashVolumeStore::scratchBuffer()
{
    return scratch_;
}

bool FlashVolumeStore::layoutSafe()
{
    const uint32_t firstReservedOffset =
        flashOffset(
            static_cast<Volume>(
                VolumeCount - 1
            )
        );

    const uintptr_t binaryEnd =
        reinterpret_cast<uintptr_t>(
            &__flash_binary_end
        );

    const uintptr_t storageStart =
        XIP_BASE + firstReservedOffset;

    return binaryEnd <= storageStart;
}

uint32_t FlashVolumeStore::crc32(
    const uint8_t* data,
    size_t length,
    uint32_t crc
)
{
    for (size_t i = 0;
         i < length;
         ++i)
    {
        crc ^= data[i];

        for (unsigned bit = 0;
             bit < 8;
             ++bit)
        {
            const uint32_t mask =
                0u - (crc & 1u);

            crc =
                (crc >> 1) ^
                (0xedb88320u & mask);
        }
    }

    return crc;
}

uint32_t FlashVolumeStore::recordCrc(
    const Header& header,
    const uint8_t* payload
)
{
    Header copy = header;
    copy.crc = 0;

    uint32_t crc =
        crc32(
            reinterpret_cast<const uint8_t*>(&copy),
            sizeof(copy)
        );

    crc =
        crc32(
            payload,
            header.length,
            crc
        );

    return ~crc;
}

bool FlashVolumeStore::read(
    Volume volume,
    uint16_t expectedVersion,
    void* output,
    size_t outputCapacity,
    size_t& outputLength
)
{
    outputLength = 0;

    if (!validVolume(volume) ||
        !layoutSafe() ||
        output == nullptr)
    {
        return false;
    }

    const uint8_t* flashData =
        mappedData(volume);

    Header header;

    memcpy(
        &header,
        flashData,
        sizeof(header)
    );

    if (header.magic != Magic ||
        header.version != expectedVersion ||
        header.length > PayloadCapacity ||
        header.length > outputCapacity)
    {
        return false;
    }

    const uint8_t* payload =
        flashData + sizeof(Header);

    if (recordCrc(
            header,
            payload) !=
        header.crc)
    {
        return false;
    }

    memcpy(
        output,
        payload,
        header.length
    );

    outputLength =
        header.length;

    return true;
}

bool FlashVolumeStore::writeRawVolume(
    Volume volume,
    const uint8_t* sectorData
)
{
    if (!validVolume(volume) ||
        !layoutSafe() ||
        sectorData == nullptr)
    {
        return false;
    }

    FlashWriteParameters parameters
    {
        .offset = flashOffset(volume),
        .sector = sectorData
    };

    const int result =
        flash_safe_execute(
            doWriteSector,
            &parameters,
            1000
        );

    return result == PICO_OK;
}

bool FlashVolumeStore::write(
    Volume volume,
    uint16_t version,
    const void* data,
    size_t length
)
{
    if (!validVolume(volume) ||
        !layoutSafe() ||
        data == nullptr ||
        length > PayloadCapacity)
    {
        return false;
    }

    uint8_t* sector =
        scratchBuffer();

    memset(
        sector,
        0xff,
        VolumeSize
    );

    Header header{};
    header.magic = Magic;
    header.version = version;
    header.length =
        static_cast<uint16_t>(length);
    header.sequence = 1;
    header.crc = 0;

    memcpy(
        sector + sizeof(Header),
        data,
        length
    );

    header.crc =
        recordCrc(
            header,
            sector + sizeof(Header)
        );

    memcpy(
        sector,
        &header,
        sizeof(header)
    );

    return writeRawVolume(
        volume,
        sector
    );
}

bool FlashVolumeStore::erase(
    Volume volume
)
{
    if (!validVolume(volume) ||
        !layoutSafe())
    {
        return false;
    }

    const uint32_t offset =
        flashOffset(volume);

    const int result =
        flash_safe_execute(
            doEraseSector,
            reinterpret_cast<void*>(
                static_cast<uintptr_t>(offset)
            ),
            1000
        );

    return result == PICO_OK;
}
