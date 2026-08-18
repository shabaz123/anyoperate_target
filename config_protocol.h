#pragma once

#include <cstddef>
#include <cstdint>

enum class FieldType
{
    Int16,
    UInt16,
    Float,
    String,
    HexArray
};

enum class FieldGroup
{
    Config,
    Wifi,
    Status
};

struct HexArray
{
    uint8_t* data;
    uint16_t length;
    uint16_t capacity;
};

struct Field
{
    const char* name;
    const char* label;

    FieldType type;
    FieldGroup group;

    void* data;

    // Used for String.
    // Ignored for scalar types and HexArray.
    size_t capacity;

    bool writable;

    // If true, GET returns a masked value rather than the stored value.
    bool secret = false;

    // 0x0000 and 0xffff mean volatile / not persisted.
    // Any other value is a globally unique PersistentStore identifier.
    uint16_t persistentId = 0;
};

class ProtocolTransport
{
public:
    virtual ~ProtocolTransport() = default;

    // Return 0..255 when a character is available.
    // Return -1 when nothing is available.
    virtual int readChar() = 0;

    virtual void write(
        const char* data,
        size_t length
    ) = 0;

    void writeString(const char* text);
    void writeLine(const char* text);
};

class ConfigRegistry
{
public:
    ConfigRegistry(
        Field* fields,
        size_t count
    );

    Field* find(const char* name);

    void list(
        FieldGroup group,
        ProtocolTransport& transport
    );

    size_t count() const;
    Field& at(size_t index);

    // Load all persistent fields. Missing entries leave compiled defaults.
    // Returns false only if a stored entry exists but could not be loaded.
    bool loadPersistentFields();

    // Save one field if it has a valid persistence identifier.
    // Volatile fields return true without accessing flash.
    bool savePersistentField(const Field& field);

private:
    Field* fields_;
    size_t count_;
};

class ProtocolSession
{
public:
    using ApplyHandler =
        bool (*)(const char* target);

    ProtocolSession(
        ProtocolTransport& transport,
        ConfigRegistry& registry,
        ApplyHandler applyHandler = nullptr
    );

    // Call frequently from main().
    void poll();

    static const char* typeName(FieldType type);

private:
    // Needs to be large enough for a full line
    static constexpr size_t MaxLineLength = 256;

    ProtocolTransport& transport_;
    ConfigRegistry& registry_;
    ApplyHandler applyHandler_ = nullptr;

    char line_[MaxLineLength];
    size_t length_ = 0;

    bool machineMode_ = false;
    bool overflow_ = false;

    static constexpr const char* Prompt = ">";

    static constexpr size_t HistoryCount = 10;
    static constexpr size_t HistoryLength = 63;

    char history_[HistoryCount][HistoryLength + 1] = {};
    bool historyTruncated_[HistoryCount] = {};

    size_t historyCount_ = 0;

    // -1 means we're not currently browsing history.
    // 0 is newest, 1 is previous, etc.
    int historyIndex_ = -1;

    // Escape sequence state:
    // 0 = normal
    // 1 = received ESC
    // 2 = received ESC [
    int escapeState_ = 0;

    void showPrompt();

    void addHistory(const char* line);
    void recallHistoryUp();
    void eraseInteractiveLine();


    void receiveChar(char ch);
    void finishLine();

    void processCommand(char* command);

    void toUpper(char* text);

    void commandHelp();
    void commandList(const char* argument);
    void commandGet(const char* name);
    void commandSet(char* expression);
    void commandApply(const char* argument);
    void commandStatus();

    bool writeValue(const Field& field);
    bool setValue(Field& field, const char* text);

    void sendError(const char* code);

    static constexpr size_t HexBytesPerLine = 16;

    bool hexUploadActive_ = false;
    bool hexUploadMachineMode_ = false;

    Field* hexUploadField_ = nullptr;
    HexArray* hexUploadArray_ = nullptr;

    uint16_t hexUploadExpected_ = 0;
    uint16_t hexUploadReceived_ = 0;

    void beginHexUpload(
        Field& field,
        uint16_t length,
        bool machineMode
    );

    void finishHexUploadLine(char* line);

    void showHexUploadRemaining();

    static bool parseHexDataLine(
        const char* text,
        uint8_t* output,
        size_t outputCapacity,
        size_t& outputLength
    );
};
