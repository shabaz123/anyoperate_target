#pragma once

#include <cstddef>
#include <cstdint>

enum class PersistentStatus
{
    Ok,
    NotFound,
    InvalidIdentifier,
    InvalidArgument,
    TooLong,
    NoSpace,
    TypeMismatch,
    AllocationMismatch,
    BufferTooSmall,
    Corrupt,
    FlashError,
    LayoutUnsafe
};

struct PersistentByteArrayInfo
{
    uint16_t length;
    uint16_t capacity;
    uint8_t allocatedChunks;
};

class PersistentStore
{
public:
    static constexpr uint16_t InvalidId0 = 0x0000;
    static constexpr uint16_t InvalidIdF = 0xffff;

    static constexpr size_t StringStorageBytes = 122;
    static constexpr size_t MaxStringLength = 121;

    static constexpr size_t RawChunkSize = 512;
    static constexpr uint8_t RawChunkCount = 24;
    static constexpr uint8_t FirstPayloadChunk = 1;
    static constexpr uint8_t LastPayloadChunk = 23;
    static constexpr uint8_t MaxRawAllocationChunks = 8;
    static constexpr uint16_t MaxRawLength = 4096;

    static PersistentStatus readInt16(
        uint16_t identifier,
        int16_t* value
    );

    static PersistentStatus writeInt16(
        uint16_t identifier,
        int16_t value
    );

    static PersistentStatus readUInt16(
        uint16_t identifier,
        uint16_t* value
    );

    static PersistentStatus writeUInt16(
        uint16_t identifier,
        uint16_t value
    );

    static PersistentStatus readFloat(
        uint16_t identifier,
        float* value
    );

    static PersistentStatus writeFloat(
        uint16_t identifier,
        float value
    );

    static PersistentStatus readString(
        uint16_t identifier,
        char* buffer,
        size_t bufferSize
    );

    static PersistentStatus writeString(
        uint16_t identifier,
        const char* text
    );

    static PersistentStatus getByteArrayInfo(
        uint16_t identifier,
        PersistentByteArrayInfo* info
    );

    static PersistentStatus readByteArray(
        uint16_t identifier,
        uint8_t* buffer,
        size_t bufferCapacity,
        uint16_t* actualLength
    );

    static PersistentStatus writeByteArray(
        uint16_t identifier,
        const uint8_t* data,
        uint16_t length,
        uint8_t allocationChunks
    );

    static PersistentStatus remove(
        uint16_t identifier
    );

    // Erase generic application persistence only: Volumes 1..5.
    // Wi-Fi/system Settings in Volume 0 are preserved.
    static PersistentStatus eraseAll();

    // Erase every reserved persistence volume, including Wi-Fi Volume 0.
    static PersistentStatus eraseFactory();

    static const char* statusName(
        PersistentStatus status
    );

private:
    enum class StoredType
    {
        None,
        Scalar,
        String,
        Raw,
        Multiple
    };

    struct ScalarRecord
    {
        uint16_t identifier;

        // Exact six-byte payload:
        //   Int16/UInt16 -> value at payload[0..1]
        //   Float        -> reserved at payload[0..1],
        //                   float bits at payload[2..5]
        uint8_t payload[6];
    };

    struct StringRecord
    {
        uint16_t identifier;
        char text[StringStorageBytes];
        uint8_t reserved[4];
    };

    struct RawEntry
    {
        uint16_t identifier;
        uint8_t firstChunk;
        uint8_t allocatedChunks;
        uint16_t length;
        uint8_t flags;
        uint8_t version;
        uint32_t crc;
        uint8_t reserved[4];
    };

    static_assert(sizeof(ScalarRecord) == 8);
    static_assert(sizeof(StringRecord) == 128);
    static_assert(sizeof(RawEntry) == 16);

    static constexpr size_t ScalarRecordCount = 512;
    static constexpr size_t StringRecordCount = 32;
    static constexpr size_t RawEntryCount = 24;
    static constexpr uint8_t RawEntryVersion = 1;

    static bool validIdentifier(uint16_t identifier);
    static StoredType findStoredType(uint16_t identifier);

    static int findScalarIndex(uint16_t identifier);
    static int findFreeScalarIndex();
    static int findStringIndex(uint16_t identifier);
    static int findFreeStringIndex();
    static int findRawIndex(uint16_t identifier);
    static int findFreeRawIndex();

    static bool rawEntryValid(const RawEntry& entry);
    static bool rawRangeFree(
        uint8_t firstChunk,
        uint8_t chunkCount,
        int ignoreEntryIndex
    );
    static uint8_t findRawAllocation(uint8_t chunkCount);

    static const uint8_t* rawPointer(size_t rawOffset);
    static bool writeRawSector(
        unsigned rawSectorIndex,
        const uint8_t* sector
    );

    static uint32_t crc32(
        const uint8_t* data,
        size_t length,
        uint32_t crc = 0xffffffffu
    );

    static uint32_t rawEntryCrc(const RawEntry& entry);

    static PersistentStatus typeCheckForWrite(
        uint16_t identifier,
        StoredType expected
    );
};
