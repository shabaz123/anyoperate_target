#include "persistent_store.h"

#include <cstring>

#include "flash_volume_store.h"

namespace
{
    constexpr uint16_t ErasedIdentifier = 0xffff;

    FlashVolumeStore::Volume rawVolumeFromSector(unsigned index)
    {
        switch (index)
        {
            case 0: return FlashVolumeStore::Volume::Raw0;
            case 1: return FlashVolumeStore::Volume::Raw1;
            default: return FlashVolumeStore::Volume::Raw2;
        }
    }
}

bool PersistentStore::validIdentifier(uint16_t identifier)
{
    return identifier != InvalidId0 &&
           identifier != InvalidIdF;
}

int PersistentStore::findScalarIndex(uint16_t identifier)
{
    const auto* records =
        reinterpret_cast<const ScalarRecord*>(
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Scalars));

    if (records == nullptr) return -1;

    for (size_t i = 0; i < ScalarRecordCount; ++i)
    {
        if (records[i].identifier == identifier)
            return static_cast<int>(i);
    }

    return -1;
}

int PersistentStore::findFreeScalarIndex()
{
    const auto* records =
        reinterpret_cast<const ScalarRecord*>(
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Scalars));

    if (records == nullptr) return -1;

    for (size_t i = 0; i < ScalarRecordCount; ++i)
    {
        if (records[i].identifier == ErasedIdentifier)
            return static_cast<int>(i);
    }

    return -1;
}

int PersistentStore::findStringIndex(uint16_t identifier)
{
    const auto* records =
        reinterpret_cast<const StringRecord*>(
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Strings));

    if (records == nullptr) return -1;

    for (size_t i = 0; i < StringRecordCount; ++i)
    {
        if (records[i].identifier == identifier)
            return static_cast<int>(i);
    }

    return -1;
}

int PersistentStore::findFreeStringIndex()
{
    const auto* records =
        reinterpret_cast<const StringRecord*>(
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Strings));

    if (records == nullptr) return -1;

    for (size_t i = 0; i < StringRecordCount; ++i)
    {
        if (records[i].identifier == ErasedIdentifier)
            return static_cast<int>(i);
    }

    return -1;
}

const uint8_t* PersistentStore::rawPointer(size_t rawOffset)
{
    constexpr size_t RawBytes =
        3 * FlashVolumeStore::VolumeSize;

    if (rawOffset >= RawBytes)
        return nullptr;

    const unsigned sectorIndex =
        static_cast<unsigned>(
            rawOffset / FlashVolumeStore::VolumeSize);

    const size_t offsetInSector =
        rawOffset % FlashVolumeStore::VolumeSize;

    return FlashVolumeStore::mappedData(
               rawVolumeFromSector(sectorIndex)) +
           offsetInSector;
}

int PersistentStore::findRawIndex(uint16_t identifier)
{
    const auto* entries =
        reinterpret_cast<const RawEntry*>(
            rawPointer(0));

    if (entries == nullptr) return -1;

    for (size_t i = 0; i < RawEntryCount; ++i)
    {
        if (entries[i].identifier == identifier)
            return static_cast<int>(i);
    }

    return -1;
}

int PersistentStore::findFreeRawIndex()
{
    const auto* entries =
        reinterpret_cast<const RawEntry*>(
            rawPointer(0));

    if (entries == nullptr) return -1;

    for (size_t i = 0; i < RawEntryCount; ++i)
    {
        if (entries[i].identifier == ErasedIdentifier)
            return static_cast<int>(i);
    }

    return -1;
}

PersistentStore::StoredType
PersistentStore::findStoredType(uint16_t identifier)
{
    unsigned count = 0;
    StoredType found = StoredType::None;

    if (findScalarIndex(identifier) >= 0)
    {
        ++count;
        found = StoredType::Scalar;
    }

    if (findStringIndex(identifier) >= 0)
    {
        ++count;
        found = StoredType::String;
    }

    if (findRawIndex(identifier) >= 0)
    {
        ++count;
        found = StoredType::Raw;
    }

    if (count > 1)
        return StoredType::Multiple;

    return found;
}

PersistentStatus PersistentStore::typeCheckForWrite(
    uint16_t identifier,
    StoredType expected)
{
    if (!validIdentifier(identifier))
        return PersistentStatus::InvalidIdentifier;

    if (!FlashVolumeStore::layoutSafe())
        return PersistentStatus::LayoutUnsafe;

    const StoredType existing =
        findStoredType(identifier);

    if (existing == StoredType::Multiple)
        return PersistentStatus::Corrupt;

    if (existing != StoredType::None &&
        existing != expected)
        return PersistentStatus::TypeMismatch;

    return PersistentStatus::Ok;
}

PersistentStatus PersistentStore::readUInt16(
    uint16_t identifier,
    uint16_t* value)
{
    if (!validIdentifier(identifier))
        return PersistentStatus::InvalidIdentifier;

    if (value == nullptr)
        return PersistentStatus::InvalidArgument;

    if (!FlashVolumeStore::layoutSafe())
        return PersistentStatus::LayoutUnsafe;

    const int index = findScalarIndex(identifier);

    if (index < 0)
    {
        return findStoredType(identifier) == StoredType::None
            ? PersistentStatus::NotFound
            : PersistentStatus::TypeMismatch;
    }

    const auto* records =
        reinterpret_cast<const ScalarRecord*>(
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Scalars));

    memcpy(
        value,
        records[index].payload,
        sizeof(*value));

    return PersistentStatus::Ok;
}

PersistentStatus PersistentStore::readInt16(
    uint16_t identifier,
    int16_t* value)
{
    if (value == nullptr)
        return PersistentStatus::InvalidArgument;

    uint16_t bits = 0;

    const PersistentStatus status =
        readUInt16(identifier, &bits);

    if (status != PersistentStatus::Ok)
        return status;

    memcpy(value, &bits, sizeof(bits));
    return PersistentStatus::Ok;
}

PersistentStatus PersistentStore::writeUInt16(
    uint16_t identifier,
    uint16_t value)
{
    const PersistentStatus check =
        typeCheckForWrite(
            identifier,
            StoredType::Scalar);

    if (check != PersistentStatus::Ok)
        return check;

    int index = findScalarIndex(identifier);

    if (index < 0)
    {
        index = findFreeScalarIndex();

        if (index < 0)
            return PersistentStatus::NoSpace;
    }

    uint8_t* scratch =
        FlashVolumeStore::scratchBuffer();

    memcpy(
        scratch,
        FlashVolumeStore::mappedData(
            FlashVolumeStore::Volume::Scalars),
        FlashVolumeStore::VolumeSize);

    auto* records =
        reinterpret_cast<ScalarRecord*>(scratch);

    if (records[index].identifier ==
        ErasedIdentifier)
    {
        memset(
            &records[index],
            0xff,
            sizeof(ScalarRecord));
    }

    records[index].identifier = identifier;

    memcpy(
        records[index].payload,
        &value,
        sizeof(value));

    return FlashVolumeStore::writeRawVolume(
               FlashVolumeStore::Volume::Scalars,
               scratch)
        ? PersistentStatus::Ok
        : PersistentStatus::FlashError;
}

PersistentStatus PersistentStore::writeInt16(
    uint16_t identifier,
    int16_t value)
{
    uint16_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return writeUInt16(identifier, bits);
}


PersistentStatus PersistentStore::readFloat(
    uint16_t identifier,
    float* value)
{
    if (!validIdentifier(identifier))
        return PersistentStatus::InvalidIdentifier;

    if (value == nullptr)
        return PersistentStatus::InvalidArgument;

    if (!FlashVolumeStore::layoutSafe())
        return PersistentStatus::LayoutUnsafe;

    const int index = findScalarIndex(identifier);

    if (index < 0)
    {
        return findStoredType(identifier) == StoredType::None
            ? PersistentStatus::NotFound
            : PersistentStatus::TypeMismatch;
    }

    const auto* records =
        reinterpret_cast<const ScalarRecord*>(
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Scalars));

    memcpy(
        value,
        records[index].payload + 2,
        sizeof(*value));

    return PersistentStatus::Ok;
}


PersistentStatus PersistentStore::writeFloat(
    uint16_t identifier,
    float value)
{
    const PersistentStatus check =
        typeCheckForWrite(
            identifier,
            StoredType::Scalar);

    if (check != PersistentStatus::Ok)
        return check;

    int index = findScalarIndex(identifier);

    if (index < 0)
    {
        index = findFreeScalarIndex();

        if (index < 0)
            return PersistentStatus::NoSpace;
    }

    uint8_t* scratch =
        FlashVolumeStore::scratchBuffer();

    memcpy(
        scratch,
        FlashVolumeStore::mappedData(
            FlashVolumeStore::Volume::Scalars),
        FlashVolumeStore::VolumeSize);

    auto* records =
        reinterpret_cast<ScalarRecord*>(scratch);

    //
    // For a float record:
    //
    //   +0 uint16 identifier
    //   +2 uint16 reserved
    //   +4 float value
    //
    // Reinitialize the complete 8-byte record so reserved bytes remain 0xff.
    //
    memset(
        &records[index],
        0xff,
        sizeof(ScalarRecord));

    records[index].identifier = identifier;

    memcpy(
        records[index].payload + 2,
        &value,
        sizeof(value));

    return FlashVolumeStore::writeRawVolume(
               FlashVolumeStore::Volume::Scalars,
               scratch)
        ? PersistentStatus::Ok
        : PersistentStatus::FlashError;
}


PersistentStatus PersistentStore::readString(
    uint16_t identifier,
    char* buffer,
    size_t bufferSize)
{
    if (!validIdentifier(identifier))
        return PersistentStatus::InvalidIdentifier;

    if (buffer == nullptr || bufferSize == 0)
        return PersistentStatus::InvalidArgument;

    if (!FlashVolumeStore::layoutSafe())
        return PersistentStatus::LayoutUnsafe;

    const int index = findStringIndex(identifier);

    if (index < 0)
    {
        return findStoredType(identifier) == StoredType::None
            ? PersistentStatus::NotFound
            : PersistentStatus::TypeMismatch;
    }

    const auto* records =
        reinterpret_cast<const StringRecord*>(
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Strings));

    const char* text = records[index].text;

    size_t length = 0;

    while (length < StringStorageBytes &&
           text[length] != '\0')
    {
        ++length;
    }

    if (length == StringStorageBytes)
        return PersistentStatus::Corrupt;

    if (length + 1 > bufferSize)
        return PersistentStatus::BufferTooSmall;

    memcpy(buffer, text, length + 1);
    return PersistentStatus::Ok;
}

PersistentStatus PersistentStore::writeString(
    uint16_t identifier,
    const char* text)
{
    if (text == nullptr)
        return PersistentStatus::InvalidArgument;

    const size_t length = strlen(text);

    if (length > MaxStringLength)
        return PersistentStatus::TooLong;

    const PersistentStatus check =
        typeCheckForWrite(
            identifier,
            StoredType::String);

    if (check != PersistentStatus::Ok)
        return check;

    int index = findStringIndex(identifier);

    if (index < 0)
    {
        index = findFreeStringIndex();

        if (index < 0)
            return PersistentStatus::NoSpace;
    }

    uint8_t* scratch =
        FlashVolumeStore::scratchBuffer();

    memcpy(
        scratch,
        FlashVolumeStore::mappedData(
            FlashVolumeStore::Volume::Strings),
        FlashVolumeStore::VolumeSize);

    auto* records =
        reinterpret_cast<StringRecord*>(scratch);

    memset(
        &records[index],
        0xff,
        sizeof(StringRecord));

    records[index].identifier = identifier;

    memcpy(
        records[index].text,
        text,
        length);

    records[index].text[length] = '\0';

    return FlashVolumeStore::writeRawVolume(
               FlashVolumeStore::Volume::Strings,
               scratch)
        ? PersistentStatus::Ok
        : PersistentStatus::FlashError;
}

bool PersistentStore::rawEntryValid(
    const RawEntry& entry)
{
    if (!validIdentifier(entry.identifier))
        return false;

    if (entry.version != RawEntryVersion)
        return false;

    if (entry.firstChunk < FirstPayloadChunk ||
        entry.firstChunk > LastPayloadChunk)
        return false;

    if (entry.allocatedChunks == 0 ||
        entry.allocatedChunks >
            MaxRawAllocationChunks)
        return false;

    const unsigned lastChunk =
        static_cast<unsigned>(entry.firstChunk) +
        entry.allocatedChunks - 1u;

    if (lastChunk > LastPayloadChunk)
        return false;

    if (entry.length >
        static_cast<uint16_t>(
            entry.allocatedChunks *
            RawChunkSize))
        return false;

    return true;
}

bool PersistentStore::rawRangeFree(
    uint8_t firstChunk,
    uint8_t chunkCount,
    int ignoreEntryIndex)
{
    const auto* entries =
        reinterpret_cast<const RawEntry*>(
            rawPointer(0));

    const unsigned candidateFirst = firstChunk;
    const unsigned candidateLast =
        candidateFirst + chunkCount - 1u;

    for (size_t i = 0; i < RawEntryCount; ++i)
    {
        if (static_cast<int>(i) ==
            ignoreEntryIndex)
            continue;

        if (entries[i].identifier ==
            ErasedIdentifier)
            continue;

        if (!rawEntryValid(entries[i]))
        {
            // Malformed allocated entry: do not trust its range.
            // The store should be treated as corrupt rather than
            // trying to allocate around unknown metadata.
            return false;
        }

        const unsigned usedFirst =
            entries[i].firstChunk;

        const unsigned usedLast =
            usedFirst +
            entries[i].allocatedChunks - 1u;

        if (candidateFirst <= usedLast &&
            candidateLast >= usedFirst)
            return false;
    }

    return true;
}

uint8_t PersistentStore::findRawAllocation(
    uint8_t chunkCount)
{
    if (chunkCount == 0 ||
        chunkCount > MaxRawAllocationChunks)
        return 0;

    for (unsigned first = FirstPayloadChunk;
         first + chunkCount - 1u <=
             LastPayloadChunk;
         ++first)
    {
        if (rawRangeFree(
                static_cast<uint8_t>(first),
                chunkCount,
                -1))
        {
            return static_cast<uint8_t>(first);
        }
    }

    return 0;
}

bool PersistentStore::writeRawSector(
    unsigned rawSectorIndex,
    const uint8_t* sector)
{
    if (rawSectorIndex >= 3)
        return false;

    return FlashVolumeStore::writeRawVolume(
        rawVolumeFromSector(rawSectorIndex),
        sector);
}

uint32_t PersistentStore::crc32(
    const uint8_t* data,
    size_t length,
    uint32_t crc)
{
    for (size_t i = 0; i < length; ++i)
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

uint32_t PersistentStore::rawEntryCrc(
    const RawEntry& entry)
{
    RawEntry copy = entry;
    copy.crc = 0;

    uint32_t crc =
        crc32(
            reinterpret_cast<const uint8_t*>(&copy),
            sizeof(copy));

    size_t remaining = entry.length;

    size_t rawOffset =
        static_cast<size_t>(
            entry.firstChunk) *
        RawChunkSize;

    while (remaining != 0)
    {
        const size_t offsetInSector =
            rawOffset %
            FlashVolumeStore::VolumeSize;

        const size_t available =
            FlashVolumeStore::VolumeSize -
            offsetInSector;

        const size_t count =
            remaining < available
                ? remaining
                : available;

        crc =
            crc32(
                rawPointer(rawOffset),
                count,
                crc);

        rawOffset += count;
        remaining -= count;
    }

    return ~crc;
}

PersistentStatus PersistentStore::getByteArrayInfo(
    uint16_t identifier,
    PersistentByteArrayInfo* info)
{
    if (!validIdentifier(identifier))
        return PersistentStatus::InvalidIdentifier;

    if (info == nullptr)
        return PersistentStatus::InvalidArgument;

    if (!FlashVolumeStore::layoutSafe())
        return PersistentStatus::LayoutUnsafe;

    const int index = findRawIndex(identifier);

    if (index < 0)
    {
        return findStoredType(identifier) == StoredType::None
            ? PersistentStatus::NotFound
            : PersistentStatus::TypeMismatch;
    }

    const auto* entries =
        reinterpret_cast<const RawEntry*>(
            rawPointer(0));

    const RawEntry& entry = entries[index];

    if (!rawEntryValid(entry))
        return PersistentStatus::Corrupt;

    info->length = entry.length;
    info->allocatedChunks =
        entry.allocatedChunks;
    info->capacity =
        static_cast<uint16_t>(
            entry.allocatedChunks *
            RawChunkSize);

    return PersistentStatus::Ok;
}

PersistentStatus PersistentStore::readByteArray(
    uint16_t identifier,
    uint8_t* buffer,
    size_t bufferCapacity,
    uint16_t* actualLength)
{
    if (actualLength == nullptr)
        return PersistentStatus::InvalidArgument;

    *actualLength = 0;

    PersistentByteArrayInfo info;

    const PersistentStatus status =
        getByteArrayInfo(identifier, &info);

    if (status != PersistentStatus::Ok)
        return status;

    if (info.length != 0 &&
        buffer == nullptr)
        return PersistentStatus::InvalidArgument;

    if (bufferCapacity < info.length)
        return PersistentStatus::BufferTooSmall;

    const int index = findRawIndex(identifier);

    const auto* entries =
        reinterpret_cast<const RawEntry*>(
            rawPointer(0));

    const RawEntry& entry = entries[index];

    if (rawEntryCrc(entry) != entry.crc)
        return PersistentStatus::Corrupt;

    size_t remaining = entry.length;

    size_t rawOffset =
        static_cast<size_t>(
            entry.firstChunk) *
        RawChunkSize;

    size_t outputOffset = 0;

    while (remaining != 0)
    {
        const size_t offsetInSector =
            rawOffset %
            FlashVolumeStore::VolumeSize;

        const size_t available =
            FlashVolumeStore::VolumeSize -
            offsetInSector;

        const size_t count =
            remaining < available
                ? remaining
                : available;

        memcpy(
            buffer + outputOffset,
            rawPointer(rawOffset),
            count);

        rawOffset += count;
        outputOffset += count;
        remaining -= count;
    }

    *actualLength = entry.length;
    return PersistentStatus::Ok;
}

PersistentStatus PersistentStore::writeByteArray(
    uint16_t identifier,
    const uint8_t* data,
    uint16_t length,
    uint8_t allocationChunks)
{
    if (length != 0 &&
        data == nullptr)
        return PersistentStatus::InvalidArgument;

    if (allocationChunks == 0 ||
        allocationChunks >
            MaxRawAllocationChunks)
        return PersistentStatus::InvalidArgument;

    const uint16_t capacity =
        static_cast<uint16_t>(
            allocationChunks *
            RawChunkSize);

    if (length > capacity)
        return PersistentStatus::TooLong;

    const PersistentStatus check =
        typeCheckForWrite(
            identifier,
            StoredType::Raw);

    if (check != PersistentStatus::Ok)
        return check;

    int entryIndex = findRawIndex(identifier);
    uint8_t firstChunk = 0;

    if (entryIndex >= 0)
    {
        const auto* entries =
            reinterpret_cast<const RawEntry*>(
                rawPointer(0));

        const RawEntry& existing =
            entries[entryIndex];

        if (!rawEntryValid(existing))
            return PersistentStatus::Corrupt;

        if (existing.allocatedChunks !=
            allocationChunks)
            return PersistentStatus::AllocationMismatch;

        firstChunk = existing.firstChunk;
    }
    else
    {
        entryIndex = findFreeRawIndex();

        if (entryIndex < 0)
            return PersistentStatus::NoSpace;

        firstChunk =
            findRawAllocation(
                allocationChunks);

        if (firstChunk == 0)
            return PersistentStatus::NoSpace;
    }

    RawEntry newEntry;
    memset(&newEntry, 0xff, sizeof(newEntry));

    newEntry.identifier = identifier;
    newEntry.firstChunk = firstChunk;
    newEntry.allocatedChunks = allocationChunks;
    newEntry.length = length;
    newEntry.flags = 0;
    newEntry.version = RawEntryVersion;
    newEntry.crc = 0;

    const size_t allocationStart =
        static_cast<size_t>(firstChunk) *
        RawChunkSize;

    const size_t allocationBytes =
        static_cast<size_t>(
            allocationChunks) *
        RawChunkSize;

    const unsigned firstSector =
        static_cast<unsigned>(
            allocationStart /
            FlashVolumeStore::VolumeSize);

    const unsigned lastSector =
        static_cast<unsigned>(
            (allocationStart +
             allocationBytes - 1) /
            FlashVolumeStore::VolumeSize);

    //
    // Program Raw1/Raw2 before Raw0. The directory is in Raw0,
    // so the metadata is committed last.
    //
    for (int sectorIndex =
             static_cast<int>(lastSector);
         sectorIndex >=
             static_cast<int>(firstSector);
         --sectorIndex)
    {
        if (sectorIndex == 0)
            continue;

        uint8_t* scratch =
            FlashVolumeStore::scratchBuffer();

        memcpy(
            scratch,
            FlashVolumeStore::mappedData(
                rawVolumeFromSector(
                    static_cast<unsigned>(
                        sectorIndex))),
            FlashVolumeStore::VolumeSize);

        const size_t sectorStart =
            static_cast<size_t>(
                sectorIndex) *
            FlashVolumeStore::VolumeSize;

        const size_t writeStart =
            allocationStart > sectorStart
                ? allocationStart
                : sectorStart;

        const size_t allocationEnd =
            allocationStart +
            allocationBytes;

        const size_t sectorEnd =
            sectorStart +
            FlashVolumeStore::VolumeSize;

        const size_t writeEnd =
            allocationEnd < sectorEnd
                ? allocationEnd
                : sectorEnd;

        memset(
            scratch +
                (writeStart - sectorStart),
            0xff,
            writeEnd - writeStart);

        const size_t dataStart =
            writeStart - allocationStart;

        if (dataStart < length)
        {
            const size_t dataRemaining =
                length - dataStart;

            const size_t writable =
                writeEnd - writeStart;

            const size_t copyCount =
                dataRemaining < writable
                    ? dataRemaining
                    : writable;

            memcpy(
                scratch +
                    (writeStart -
                     sectorStart),
                data + dataStart,
                copyCount);
        }

        if (!writeRawSector(
                static_cast<unsigned>(
                    sectorIndex),
                scratch))
            return PersistentStatus::FlashError;
    }

    //
    // Raw0 may contain both payload and directory. Modify both in
    // one sector image and commit it last.
    //
    uint8_t* scratch =
        FlashVolumeStore::scratchBuffer();

    memcpy(
        scratch,
        FlashVolumeStore::mappedData(
            FlashVolumeStore::Volume::Raw0),
        FlashVolumeStore::VolumeSize);

    if (firstSector == 0)
    {
        const size_t writeStart =
            allocationStart;

        const size_t allocationEnd =
            allocationStart +
            allocationBytes;

        const size_t writeEnd =
            allocationEnd <
                    FlashVolumeStore::VolumeSize
                ? allocationEnd
                : FlashVolumeStore::VolumeSize;

        memset(
            scratch + writeStart,
            0xff,
            writeEnd - writeStart);

        const size_t copyCount =
            length <
                    (writeEnd -
                     writeStart)
                ? length
                : (writeEnd -
                   writeStart);

        if (copyCount != 0)
        {
            memcpy(
                scratch + writeStart,
                data,
                copyCount);
        }
    }

    //
    // CRC protects both metadata and active payload bytes.
    //
    RawEntry crcEntry = newEntry;
    crcEntry.crc = 0;

    uint32_t crc =
        crc32(
            reinterpret_cast<const uint8_t*>(
                &crcEntry),
            sizeof(crcEntry));

    size_t remaining = length;
    size_t rawOffset = allocationStart;

    while (remaining != 0)
    {
        const unsigned sectorIndex =
            static_cast<unsigned>(
                rawOffset /
                FlashVolumeStore::VolumeSize);

        const size_t offsetInSector =
            rawOffset %
            FlashVolumeStore::VolumeSize;

        const size_t available =
            FlashVolumeStore::VolumeSize -
            offsetInSector;

        const size_t count =
            remaining < available
                ? remaining
                : available;

        const uint8_t* source =
            sectorIndex == 0
                ? scratch + offsetInSector
                : rawPointer(rawOffset);

        crc = crc32(source, count, crc);

        rawOffset += count;
        remaining -= count;
    }

    newEntry.crc = ~crc;

    auto* entries =
        reinterpret_cast<RawEntry*>(scratch);

    entries[entryIndex] = newEntry;

    if (!writeRawSector(0, scratch))
        return PersistentStatus::FlashError;

    return PersistentStatus::Ok;
}

PersistentStatus PersistentStore::remove(
    uint16_t identifier)
{
    if (!validIdentifier(identifier))
        return PersistentStatus::InvalidIdentifier;

    if (!FlashVolumeStore::layoutSafe())
        return PersistentStatus::LayoutUnsafe;

    const StoredType type =
        findStoredType(identifier);

    if (type == StoredType::None)
        return PersistentStatus::NotFound;

    if (type == StoredType::Multiple)
        return PersistentStatus::Corrupt;

    uint8_t* scratch =
        FlashVolumeStore::scratchBuffer();

    if (type == StoredType::Scalar)
    {
        const int index =
            findScalarIndex(identifier);

        memcpy(
            scratch,
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Scalars),
            FlashVolumeStore::VolumeSize);

        auto* records =
            reinterpret_cast<ScalarRecord*>(
                scratch);

        memset(
            &records[index],
            0xff,
            sizeof(ScalarRecord));

        return FlashVolumeStore::writeRawVolume(
                   FlashVolumeStore::Volume::Scalars,
                   scratch)
            ? PersistentStatus::Ok
            : PersistentStatus::FlashError;
    }

    if (type == StoredType::String)
    {
        const int index =
            findStringIndex(identifier);

        memcpy(
            scratch,
            FlashVolumeStore::mappedData(
                FlashVolumeStore::Volume::Strings),
            FlashVolumeStore::VolumeSize);

        auto* records =
            reinterpret_cast<StringRecord*>(
                scratch);

        memset(
            &records[index],
            0xff,
            sizeof(StringRecord));

        return FlashVolumeStore::writeRawVolume(
                   FlashVolumeStore::Volume::Strings,
                   scratch)
            ? PersistentStatus::Ok
            : PersistentStatus::FlashError;
    }

    const int index =
        findRawIndex(identifier);

    memcpy(
        scratch,
        FlashVolumeStore::mappedData(
            FlashVolumeStore::Volume::Raw0),
        FlashVolumeStore::VolumeSize);

    auto* entries =
        reinterpret_cast<RawEntry*>(scratch);

    memset(
        &entries[index],
        0xff,
        sizeof(RawEntry));

    return FlashVolumeStore::writeRawVolume(
               FlashVolumeStore::Volume::Raw0,
               scratch)
        ? PersistentStatus::Ok
        : PersistentStatus::FlashError;
}


PersistentStatus PersistentStore::eraseAll()
{
    if (!FlashVolumeStore::layoutSafe())
        return PersistentStatus::LayoutUnsafe;

    const FlashVolumeStore::Volume volumes[] =
    {
        FlashVolumeStore::Volume::Scalars,
        FlashVolumeStore::Volume::Strings,
        FlashVolumeStore::Volume::Raw0,
        FlashVolumeStore::Volume::Raw1,
        FlashVolumeStore::Volume::Raw2
    };

    for (const auto volume : volumes)
    {
        if (!FlashVolumeStore::erase(volume))
            return PersistentStatus::FlashError;
    }

    return PersistentStatus::Ok;
}


PersistentStatus PersistentStore::eraseFactory()
{
    const PersistentStatus appStatus =
        eraseAll();

    if (appStatus != PersistentStatus::Ok)
        return appStatus;

    if (!FlashVolumeStore::erase(
            FlashVolumeStore::Volume::Settings))
    {
        return PersistentStatus::FlashError;
    }

    return PersistentStatus::Ok;
}


const char* PersistentStore::statusName(
    PersistentStatus status)
{
    switch (status)
    {
        case PersistentStatus::Ok:
            return "OK";
        case PersistentStatus::NotFound:
            return "NOT_FOUND";
        case PersistentStatus::InvalidIdentifier:
            return "INVALID_IDENTIFIER";
        case PersistentStatus::InvalidArgument:
            return "INVALID_ARGUMENT";
        case PersistentStatus::TooLong:
            return "TOO_LONG";
        case PersistentStatus::NoSpace:
            return "NO_SPACE";
        case PersistentStatus::TypeMismatch:
            return "TYPE_MISMATCH";
        case PersistentStatus::AllocationMismatch:
            return "ALLOCATION_MISMATCH";
        case PersistentStatus::BufferTooSmall:
            return "BUFFER_TOO_SMALL";
        case PersistentStatus::Corrupt:
            return "CORRUPT";
        case PersistentStatus::FlashError:
            return "FLASH_ERROR";
        case PersistentStatus::LayoutUnsafe:
            return "LAYOUT_UNSAFE";
    }

    return "UNKNOWN";
}
