#include "config_protocol.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "persistent_store.h"


static bool parseHex(const char* text, uint32_t& value, unsigned digits)
{
    if (text[0] != '0' ||
        (text[1] != 'x' && text[1] != 'X'))
    {
        return false;
    }

    text += 2;

    if (strlen(text) != digits)
    {
        return false;
    }

    char* end = nullptr;

    const unsigned long parsed =
        strtoul(text, &end, 16);

    if (*end != '\0')
    {
        return false;
    }

    value = static_cast<uint32_t>(parsed);
    return true;
}

static int hexNibble(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';

    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;

    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;

    return -1;
}


ConfigRegistry::ConfigRegistry(Field* fields, size_t count)
    : fields_(fields), count_(count)
{
}


Field* ConfigRegistry::find(const char* name)
{
    for (size_t i = 0; i < count_; ++i) {
        if (strcmp(fields_[i].name, name) == 0) {
            return &fields_[i];
        }
    }

    return nullptr;
}

size_t ConfigRegistry::count() const
{
    return count_;
}


Field& ConfigRegistry::at(size_t index)
{
    return fields_[index];
}


static bool fieldHasPersistentId(
    const Field& field)
{
    return field.persistentId != 0x0000 &&
           field.persistentId != 0xffff;
}


bool ConfigRegistry::loadPersistentFields()
{
    bool success = true;

    for (size_t i = 0; i < count_; ++i)
    {
        Field& field = fields_[i];

        if (!fieldHasPersistentId(field))
            continue;

        PersistentStatus status =
            PersistentStatus::NotFound;

        switch (field.type)
        {
            case FieldType::Int16:
                status =
                    PersistentStore::readInt16(
                        field.persistentId,
                        static_cast<int16_t*>(field.data));
                break;

            case FieldType::UInt16:
                status =
                    PersistentStore::readUInt16(
                        field.persistentId,
                        static_cast<uint16_t*>(field.data));
                break;

            case FieldType::Float:
                status =
                    PersistentStore::readFloat(
                        field.persistentId,
                        static_cast<float*>(field.data));
                break;

            case FieldType::String:
                status =
                    PersistentStore::readString(
                        field.persistentId,
                        static_cast<char*>(field.data),
                        field.capacity);
                break;

            case FieldType::HexArray:
            {
                auto& array =
                    *static_cast<HexArray*>(field.data);

                uint16_t actualLength = 0;

                status =
                    PersistentStore::readByteArray(
                        field.persistentId,
                        array.data,
                        array.capacity,
                        &actualLength);

                if (status == PersistentStatus::Ok)
                {
                    array.length =
                        actualLength;
                }

                break;
            }
        }

        //
        // NotFound is normal: retain the compiled/default RAM value.
        //
        if (status != PersistentStatus::Ok &&
            status != PersistentStatus::NotFound)
        {
            success = false;
        }
    }

    return success;
}


bool ConfigRegistry::savePersistentField(
    const Field& field)
{
    if (!fieldHasPersistentId(field))
        return true;

    PersistentStatus status =
        PersistentStatus::InvalidArgument;

    switch (field.type)
    {
        case FieldType::Int16:
            status =
                PersistentStore::writeInt16(
                    field.persistentId,
                    *static_cast<const int16_t*>(field.data));
            break;

        case FieldType::UInt16:
            status =
                PersistentStore::writeUInt16(
                    field.persistentId,
                    *static_cast<const uint16_t*>(field.data));
            break;

        case FieldType::Float:
            status =
                PersistentStore::writeFloat(
                    field.persistentId,
                    *static_cast<const float*>(field.data));
            break;

        case FieldType::String:
            status =
                PersistentStore::writeString(
                    field.persistentId,
                    static_cast<const char*>(field.data));
            break;

        case FieldType::HexArray:
        {
            const auto& array =
                *static_cast<const HexArray*>(field.data);

            const uint8_t chunks =
                static_cast<uint8_t>(
                    (array.capacity +
                     PersistentStore::RawChunkSize - 1) /
                    PersistentStore::RawChunkSize);

            if (chunks == 0 ||
                chunks >
                    PersistentStore::MaxRawAllocationChunks)
            {
                return false;
            }

            status =
                PersistentStore::writeByteArray(
                    field.persistentId,
                    array.data,
                    array.length,
                    chunks);

            break;
        }
    }

    return status == PersistentStatus::Ok;
}

void ConfigRegistry::list(FieldGroup group, ProtocolTransport& transport)
{
    char buffer[160];

    for (size_t i = 0; i < count_; ++i) {
        const Field& field = fields_[i];

        if (field.group != group) {
            continue;
        }

        snprintf(
            buffer,
            sizeof(buffer),
            "%s|%s|%s%s",
            field.name,
            field.label,
            ProtocolSession::typeName(field.type),
            field.secret ? "|secret" : ""
        );

        transport.writeLine(buffer);
    }

    transport.writeLine("#END");
}


void ProtocolTransport::writeString(const char* text)
{
    write(text, strlen(text));
}


void ProtocolTransport::writeLine(const char* text)
{
    writeString(text);
    write("\r\n", 2);
}


ProtocolSession::ProtocolSession(
    ProtocolTransport& transport,
    ConfigRegistry& registry,
    ApplyHandler applyHandler
)
    : transport_(transport),
      registry_(registry),
      applyHandler_(applyHandler)
{
}

void ProtocolSession::showPrompt()
{
    transport_.writeString(Prompt);
}


void ProtocolSession::addHistory(const char* line)
{
    if (line == nullptr || line[0] == '\0') {
        return;
    }

    const size_t originalLength = strlen(line);

    //
    // Shift older entries down.
    //
    const size_t moveCount =
        historyCount_ < HistoryCount
            ? historyCount_
            : HistoryCount - 1;

    for (size_t i = moveCount; i > 0; --i) {
        memcpy(
            history_[i],
            history_[i - 1],
            sizeof(history_[i])
        );

        historyTruncated_[i] =
            historyTruncated_[i - 1];
    }

    const size_t copyLength =
        originalLength > HistoryLength
            ? HistoryLength
            : originalLength;

    memcpy(
        history_[0],
        line,
        copyLength
    );

    history_[0][copyLength] = '\0';

    historyTruncated_[0] =
        originalLength > HistoryLength;

    if (historyCount_ < HistoryCount) {
        ++historyCount_;
    }

    historyIndex_ = -1;
}


void ProtocolSession::eraseInteractiveLine()
{
    //
    // Terminal-style erase: backspace, space, backspace.
    //
    for (size_t i = 0; i < length_; ++i) {
        transport_.write("\b \b", 3);
    }

    length_ = 0;
}


void ProtocolSession::recallHistoryUp()
{
    if (historyCount_ == 0) {
        return;
    }

    if (historyIndex_ + 1 <
        static_cast<int>(historyCount_)) {
        ++historyIndex_;
    }

    eraseInteractiveLine();

    const char* stored =
        history_[historyIndex_];

    const size_t storedLength =
        strlen(stored);

    memcpy(
        line_,
        stored,
        storedLength
    );

    length_ = storedLength;

    //
    // If the original entry was longer than 63 characters,
    // explicitly make the recalled command incomplete.
    //
    if (historyTruncated_[historyIndex_]) {
        static constexpr char marker[] =
            "<truncated>";

        const size_t markerLength =
            sizeof(marker) - 1;

        if (length_ + markerLength <
            MaxLineLength) {
            memcpy(
                line_ + length_,
                marker,
                markerLength
            );

            length_ += markerLength;
        }
    }

    line_[length_] = '\0';

    transport_.write(
        line_,
        length_
    );
}

bool ProtocolSession::writeValue(
    const Field& field
)
{
    char buffer[128];

    switch (field.type)
    {
        case FieldType::Int16:
        {
            uint16_t bits;

            memcpy(
                &bits,
                field.data,
                sizeof(bits)
            );

            snprintf(
                buffer,
                sizeof(buffer),
                "0x%04X",
                static_cast<unsigned>(bits)
            );

            transport_.writeString(buffer);
            return true;
        }

        case FieldType::UInt16:
        {
            const auto value =
                *static_cast<uint16_t*>(field.data);

            snprintf(
                buffer,
                sizeof(buffer),
                "0x%04X",
                static_cast<unsigned>(value)
            );

            transport_.writeString(buffer);
            return true;
        }

        case FieldType::Float:
        {
            uint32_t bits;

            memcpy(
                &bits,
                field.data,
                sizeof(bits)
            );

            snprintf(
                buffer,
                sizeof(buffer),
                "0x%08lX",
                static_cast<unsigned long>(bits)
            );

            transport_.writeString(buffer);
            return true;
        }

        case FieldType::String:
        {
            transport_.writeString(
                static_cast<const char*>(field.data)
            );

            return true;
        }

        case FieldType::HexArray:
        {
            const auto& value =
                *static_cast<const HexArray*>(field.data);

            //
            // Big-endian 16-bit length prefix.
            //
            const uint8_t high =
                static_cast<uint8_t>(
                    value.length >> 8
                );

            const uint8_t low =
                static_cast<uint8_t>(
                    value.length
                );

            auto writeByte =
                [this](uint8_t b)
                {
                    static const char hex[] =
                        "0123456789ABCDEF";

                    char text[2];

                    text[0] = hex[b >> 4];
                    text[1] = hex[b & 0x0f];

                    transport_.write(text, 2);
                };

            writeByte(high);
            writeByte(low);

            for (uint16_t i = 0;
                 i < value.length;
                 ++i)
            {
                writeByte(value.data[i]);
            }

            return true;
        }
    }

    return false;
}

bool ProtocolSession::setValue(
    Field& field,
    const char* text
)
{
    switch (field.type)
    {
        case FieldType::Int16:
        {
            uint32_t bits;

            if (!parseHex(text, bits, 4))
            {
                return false;
            }

            const uint16_t raw =
                static_cast<uint16_t>(bits);

            memcpy(
                field.data,
                &raw,
                sizeof(raw)
            );

            return true;
        }

        case FieldType::UInt16:
        {
            uint32_t value;

            if (!parseHex(text, value, 4))
            {
                return false;
            }

            *static_cast<uint16_t*>(field.data) =
                static_cast<uint16_t>(value);

            return true;
        }

        case FieldType::Float:
        {
            uint32_t bits;

            if (!parseHex(text, bits, 8))
            {
                return false;
            }

            memcpy(
                field.data,
                &bits,
                sizeof(bits)
            );

            return true;
        }

        case FieldType::String:
        {
            const size_t length = strlen(text);

            if (length >= field.capacity)
            {
                return false;
            }

            memcpy(
                field.data,
                text,
                length + 1
            );

            return true;
        }

        case FieldType::HexArray:
        {
            // HexArray SET is handled as a streamed transfer
            // by commandSet(), not here.
            return false;
        }
    }

    return false;
}

void ProtocolSession::poll()
{
    while (true) {
        const int ch = transport_.readChar();

        if (ch < 0) {
            return;
        }

        receiveChar(static_cast<char>(ch));
    }
}


void ProtocolSession::receiveChar(char ch)
{
    const bool suppressEcho =
        machineMode_ ||
        (hexUploadActive_ &&
        hexUploadMachineMode_);
    
    //
    // '$' always starts a fresh machine-mode command.
    //
    // If we were in the middle of a partial command, discard it
    // and resynchronise here.
    //
    if (ch == '$' && !hexUploadActive_)
    {
        length_ = 0;
        overflow_ = false;
        escapeState_ = 0;
        historyIndex_ = -1;

        machineMode_ = true;

        return;
    }

    //
    // ----------------------------------------------------------
    // ANSI escape sequences (interactive mode only)
    // ----------------------------------------------------------
    //
    // MobaXterm normally sends Up Arrow as:
    //
    // ESC [ A
    //
    if (!suppressEcho && !hexUploadActive_) {

        if (escapeState_ == 1) {

            if (ch == '[') {
                escapeState_ = 2;
            }
            else {
                escapeState_ = 0;
            }

            return;
        }

        if (escapeState_ == 2) {

            if (ch == 'A') {
                recallHistoryUp();
            }

            // Ignore other escape sequences for now.
            escapeState_ = 0;
            return;
        }

        if (ch == 0x1b) {
            escapeState_ = 1;
            return;
        }
    }

    //
    // ----------------------------------------------------------
    // End of line.
    // ----------------------------------------------------------
    //
    if (ch == '\r' || ch == '\n') {

        //
        // Empty line.
        //
        if (length_ == 0 && !overflow_) {

            if (length_ == 0 && !overflow_)
            {
                if (hexUploadActive_)
                {
                    if (!suppressEcho)
                    {
                        transport_.write("\r\n", 2);
                        showHexUploadRemaining();
                    }

                    return;
                }

                if (!suppressEcho)
                {
                    transport_.write("\r\n", 2);
                    showPrompt();
                }

                machineMode_ = false;
                historyIndex_ = -1;
                escapeState_ = 0;

                return;
            }

            machineMode_ = false;
            historyIndex_ = -1;
            escapeState_ = 0;

            return;
        }

        if (!suppressEcho) {
            transport_.write("\r\n", 2);
        }

        finishLine();
        return;
    }

    //
    // ----------------------------------------------------------
    // Interactive backspace.
    // ----------------------------------------------------------
    //
    if ((ch == '\b' || ch == 0x7f) &&
        !suppressEcho) {

        if (length_ > 0) {
            --length_;
            transport_.write("\b \b", 3);
        }

        historyIndex_ = -1;
        return;
    }

    //
    // ----------------------------------------------------------
    // Human console echo.
    // ----------------------------------------------------------
    //
    if (!suppressEcho)
    {
        transport_.write(&ch, 1);

        if (!hexUploadActive_)
        {
            historyIndex_ = -1;
        }
    }

    //
    // ----------------------------------------------------------
    // Command buffer.
    // ----------------------------------------------------------
    //
    if (length_ >= MaxLineLength - 1) {
        overflow_ = true;
        return;
    }

    line_[length_++] = ch;
}

void ProtocolSession::finishLine()
{
    //
    // If we are currently receiving hex-array data,
    // this line is data, not a normal command.
    //
    if (hexUploadActive_)
    {
        if (overflow_)
        {
            sendError("LINE_TOO_LONG");
        }
        else
        {
            line_[length_] = '\0';

            if (length_ != 0)
            {
                finishHexUploadLine(line_);
            }
        }

        length_ = 0;
        machineMode_ = false;
        overflow_ = false;
        escapeState_ = 0;
        historyIndex_ = -1;

        return;
    }

    //
    // Normal command handling.
    //
    const bool wasMachineMode =
        machineMode_;

    if (overflow_)
    {
        sendError("LINE_TOO_LONG");
    }
    else
    {
        line_[length_] = '\0';

        if (length_ != 0)
        {
            //
            // Only human-entered commands go into history.
            //
            if (!wasMachineMode)
            {
                addHistory(line_);
            }

            //
            // Lines beginning with '#' are comments.
            //
            if (line_[0] != '#')
            {
                processCommand(line_);
            }
        }
    }

    length_ = 0;
    machineMode_ = false;
    overflow_ = false;
    escapeState_ = 0;
    historyIndex_ = -1;

    if (!wasMachineMode)
    {
        //
        // Do not show the normal prompt if processing the SET
        // command has just started a hex upload.
        //
        if (!hexUploadActive_)
        {
            showPrompt();
        }
    }
}

void ProtocolSession::toUpper(char* text)
{
    for (char* p = text; *p; ++p) {
        if (*p >= 'a' && *p <= 'z') {
            *p -= 'a' - 'A';
        }
    }
}

void ProtocolSession::processCommand(char* command)
{
    // assume the longest command is 24 char
    char upper[25];
    strncpy(upper, command, sizeof(upper) - 1);
    upper[sizeof(upper) - 1] = '\0';
    toUpper(upper);

    if (strncmp(upper, "CONNECT ", 8) == 0) {
        transport_.writeLine("OK");
        return;
    }

    if (strncmp(upper, "LIST ", 5) == 0) {
        commandList(command + 5);
        return;
    }

    if (strncmp(upper, "GET ", 4) == 0) {
        commandGet(command + 4);
        return;
    }

    if (strncmp(upper, "SET ", 4) == 0) {
        commandSet(command + 4);
        return;
    }

    if (strncmp(upper, "APPLY ", 6) == 0) {
        commandApply(command + 6);
        return;
    }

    if (strcmp(upper, "STATUS") == 0) {
        commandStatus();
        return;
    }

    if (strcmp(upper, "HELP") == 0) {
        commandHelp();
        return;
    }

    sendError("UNKNOWN_COMMAND");
}

void ProtocolSession::commandHelp()
{
    transport_.writeLine("Available commands:");
    transport_.writeLine("  CONNECT <name>");
    transport_.writeLine("  LIST <CONFIG|STATUS|WIFI>");
    transport_.writeLine("  GET <name>");
    transport_.writeLine("  SET <name>=<value>");
    transport_.writeLine("  APPLY <WIFI|ERASE|FACTORY|REBOOT>");
    transport_.writeLine("  STATUS");
    transport_.writeLine("  HELP");
}

void ProtocolSession::commandList(const char* argument)
{
    char upper[25];
    strncpy(upper, argument, sizeof(upper) - 1);
    upper[sizeof(upper) - 1] = '\0';
    toUpper(upper);


    if (strcmp(upper, "CONFIG") == 0) {
        registry_.list(
            FieldGroup::Config,
            transport_
        );

        return;
    }

    if (strcmp(upper, "WIFI") == 0) {
        registry_.list(
            FieldGroup::Wifi,
            transport_
        );

        return;
    }

    if (strcmp(upper, "STATUS") == 0) {
        registry_.list(
            FieldGroup::Status,
            transport_
        );

        return;
    }

    sendError("BAD_LIST");
}


void ProtocolSession::commandGet(const char* name)
{
    Field* field = registry_.find(name);

    if (field == nullptr)
    {
        sendError("NOT_FOUND");
        return;
    }

    transport_.writeString(field->name);
    transport_.writeString("=");

    if (field->secret)
    {
        transport_.writeString("********");
    }
    else if (!writeValue(*field))
    {
        sendError("ENCODE");
        return;
    }

    // now write in decimal if not in machine mode
    if (!machineMode_ && !field->secret) {
        if (field->type == FieldType::Int16) {
            const int16_t value =
                *static_cast<int16_t*>(field->data);

            char buffer[32];
            snprintf(buffer, sizeof(buffer), " (%d)", value);
            transport_.writeString(buffer);
        }
        else if (field->type == FieldType::UInt16) {
            const uint16_t value =
                *static_cast<uint16_t*>(field->data);

            char buffer[32];
            snprintf(buffer, sizeof(buffer), " (%u)", value);
            transport_.writeString(buffer);
        }
        else if (field->type == FieldType::Float) {
            const float value =
                *static_cast<float*>(field->data);

            char buffer[32];
            snprintf(buffer, sizeof(buffer), " (%f)", value);
            transport_.writeString(buffer);
        }
    }

    transport_.write("\r\n", 2);
}


void ProtocolSession::commandSet(
    char* expression
)
{
    char* equals = strchr(expression, '=');

    if (equals == nullptr)
    {
        sendError("BAD_SET");
        return;
    }

    *equals = '\0';

    const char* name = expression;
    const char* value = equals + 1;

    Field* field = registry_.find(name);

    if (field == nullptr)
    {
        sendError("NOT_FOUND");
        return;
    }

    if ((field->group != FieldGroup::Config &&
         field->group != FieldGroup::Wifi) ||
        !field->writable)
    {
        sendError("READ_ONLY");
        return;
    }

    if (field->type == FieldType::HexArray)
    {
        char* end = nullptr;

        const unsigned long requestedLength =
            strtoul(value, &end, 10);

        if (*value == '\0' ||
            *end != '\0')
        {
            sendError("BAD_LENGTH");
            return;
        }

        auto& array =
            *static_cast<HexArray*>(field->data);

        if (requestedLength > 4095 ||
            requestedLength > array.capacity)
        {
            sendError("BAD_LENGTH");
            return;
        }

        //
        // Special case: zero-byte array.
        //
        if (requestedLength == 0)
        {
            array.length = 0;

            if (!registry_.savePersistentField(*field))
            {
                sendError("PERSIST_FAILED");
                return;
            }

            transport_.writeLine("OK");
            return;
        }

        beginHexUpload(
            *field,
            static_cast<uint16_t>(
                requestedLength
            ),
            machineMode_
        );

        return;
    }

    if (!setValue(*field, value))
    {
        sendError("BAD_VALUE");
        return;
    }

    if (!registry_.savePersistentField(*field))
    {
        sendError("PERSIST_FAILED");
        return;
    }

    transport_.writeLine("OK");
}


void ProtocolSession::commandApply(
    const char* argument
)
{
    char upper[25];

    strncpy(
        upper,
        argument,
        sizeof(upper) - 1
    );

    upper[sizeof(upper) - 1] = '\0';
    toUpper(upper);

    if (applyHandler_ == nullptr ||
        !applyHandler_(upper))
    {
        sendError("BAD_APPLY");
        return;
    }

    // OK means the request was accepted. The application performs
    // the potentially disruptive work later from main().
    transport_.writeLine("OK");
}


void ProtocolSession::commandStatus()
{
    for (size_t i = 0; i < registry_.count(); ++i)
    {
        Field& field = registry_.at(i);

        if (field.group != FieldGroup::Status)
        {
            continue;
        }

        transport_.writeString(field.name);
        transport_.writeString("=");

        if (!writeValue(field))
        {
            sendError("ENCODE");
            return;
        }

        transport_.write("\r\n", 2);
    }

    transport_.writeLine("#END");
}


void ProtocolSession::sendError(
    const char* code
)
{
    transport_.writeString("ERROR ");
    transport_.writeLine(code);
}


const char* ProtocolSession::typeName(
    FieldType type
)
{
    switch (type)
    {
        case FieldType::Int16:
            return "int16";

        case FieldType::UInt16:
            return "uint16";

        case FieldType::Float:
            return "float";

        case FieldType::String:
            return "string";

        case FieldType::HexArray:
            return "hexarray";
    }

    return "unknown";
}

bool ProtocolSession::parseHexDataLine(
    const char* text,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
)
{
    outputLength = 0;

    int highNibble = -1;

    for (const char* p = text; *p; ++p)
    {
        if (*p == ' ' ||
            *p == '\t' ||
            *p == ',')
        {
            continue;
        }

        const int nibble = hexNibble(*p);

        if (nibble < 0)
        {
            return false;
        }

        if (highNibble < 0)
        {
            highNibble = nibble;
            continue;
        }

        if (outputLength >= outputCapacity)
        {
            return false;
        }

        output[outputLength++] =
            static_cast<uint8_t>(
                (highNibble << 4) | nibble
            );

        highNibble = -1;
    }

    // Odd number of hex digits.
    if (highNibble >= 0)
    {
        return false;
    }

    return outputLength != 0;
}

void ProtocolSession::beginHexUpload(
    Field& field,
    uint16_t length,
    bool machineMode
)
{
    auto& array =
        *static_cast<HexArray*>(field.data);

    hexUploadActive_ = true;
    hexUploadMachineMode_ = machineMode;

    hexUploadField_ = &field;
    hexUploadArray_ = &array;

    hexUploadExpected_ = length;
    hexUploadReceived_ = 0;

    //
    // We don't commit array.length until the
    // complete transfer has succeeded.
    //
    if (machineMode)
    {
        transport_.writeLine("OK");
    }
    else
    {
        showHexUploadRemaining();
    }
}

void ProtocolSession::showHexUploadRemaining()
{
    if (!hexUploadActive_ ||
        hexUploadMachineMode_)
    {
        return;
    }

    const unsigned remaining =
        hexUploadExpected_ -
        hexUploadReceived_;

    char buffer[96];

    if (remaining > HexBytesPerLine)
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "%u bytes remaining: (max: %u bytes per line)",
            remaining,
            static_cast<unsigned>(HexBytesPerLine)
        );
    }
    else
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "%u bytes remaining:",
            remaining
        );
    }

    transport_.writeLine(buffer);
}

void ProtocolSession::finishHexUploadLine(
    char* line
)
{
    uint8_t bytes[HexBytesPerLine];
    size_t byteCount = 0;

    if (!parseHexDataLine(
            line,
            bytes,
            sizeof(bytes),
            byteCount))
    {
        sendError("BAD_HEX");

        if (!hexUploadMachineMode_)
        {
            showHexUploadRemaining();
        }

        return;
    }

    const size_t remaining =
        hexUploadExpected_ -
        hexUploadReceived_;

    if (byteCount > remaining)
    {
        sendError("TOO_MUCH_DATA");

        if (!hexUploadMachineMode_)
        {
            showHexUploadRemaining();
        }

        return;
    }

    memcpy(
        hexUploadArray_->data +
            hexUploadReceived_,
        bytes,
        byteCount
    );

    hexUploadReceived_ +=
        static_cast<uint16_t>(byteCount);

    //
    // Completed?
    //
    if (hexUploadReceived_ ==
        hexUploadExpected_)
    {
        hexUploadArray_->length =
            hexUploadExpected_;

        Field* completedField =
            hexUploadField_;

        const bool persisted =
            completedField == nullptr ||
            registry_.savePersistentField(
                *completedField);

        const bool wasMachine =
            hexUploadMachineMode_;

        hexUploadActive_ = false;
        hexUploadField_ = nullptr;
        hexUploadArray_ = nullptr;

        hexUploadExpected_ = 0;
        hexUploadReceived_ = 0;
        hexUploadMachineMode_ = false;

        if (!persisted)
        {
            sendError("PERSIST_FAILED");
        }
        else
        {
            transport_.writeLine("OK");
        }

        if (!wasMachine)
        {
            showPrompt();
        }

        return;
    }

    transport_.writeLine("OK");
    showHexUploadRemaining();
}



