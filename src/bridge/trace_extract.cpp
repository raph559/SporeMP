// Offline ETW evidence extraction. Never loads or starts SPORE.
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <objbase.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
std::set<DWORD> selected;
std::ofstream output;
unsigned long long total = 0, kept = 0, decoded = 0;
std::map<std::wstring, unsigned long long> providers;

std::string utf8(const wchar_t* value) {
    if (!value) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size < 1) {
        return {};
    }
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string quote(const std::string& value) {
    std::string result = "\"";
    for (unsigned char character : value) {
        if (character == '\\' || character == '"') {
            result += '\\';
            result += static_cast<char>(character);
        } else if (character < 32) {
            char escaped[7];
            sprintf_s(escaped, "\\u%04x", character);
            result += escaped;
        } else {
            result += static_cast<char>(character);
        }
    }
    return result + '"';
}

std::string hex(const void* data, size_t length) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    constexpr char table[] = "0123456789abcdef";
    std::string text;
    text.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        text += table[bytes[i] >> 4];
        text += table[bytes[i] & 15];
    }
    return text;
}

// One top-level property of a decoded event: its name, type and, when it can be read and is under a megabyte,
// its bytes, with the text of a plain string.
void write_property(EVENT_RECORD* event, std::vector<BYTE>& schema, const EVENT_PROPERTY_INFO& property) {
    auto* name = reinterpret_cast<wchar_t*>(schema.data() + property.NameOffset);
    PROPERTY_DATA_DESCRIPTOR descriptor{};
    descriptor.PropertyName = reinterpret_cast<ULONGLONG>(name);
    descriptor.ArrayIndex = ULONG_MAX;
    ULONG length = 0;
    auto error = TdhGetPropertySize(event, 0, nullptr, 1, &descriptor, &length);
    output << "{\"name\":" << quote(utf8(name)) << ",\"flags\":" << property.Flags
           << ",\"in_type\":" << property.nonStructType.InType << ",\"error\":" << error;
    if (error == ERROR_SUCCESS && length < 1024 * 1024) {
        std::vector<BYTE> value(length + 2);
        if (TdhGetProperty(event, 0, nullptr, 1, &descriptor, length, value.data()) == ERROR_SUCCESS) {
            output << ",\"hex\":" << quote(hex(value.data(), length));
            if (property.Flags == 0 && property.nonStructType.InType == TDH_INTYPE_UNICODESTRING) {
                output << ",\"text\":" << quote(utf8(reinterpret_cast<wchar_t*>(value.data())));
            }
            if (property.Flags == 0 && property.nonStructType.InType == TDH_INTYPE_ANSISTRING) {
                output << ",\"text\":" << quote(reinterpret_cast<char*>(value.data()));
            }
        }
    }
    output << "}";
}

// The schema of an event, when the second TdhGetEventInformation call returns it: the provider, task and opcode
// names, then every top-level property. size is what the first call asked for.
void write_schema(EVENT_RECORD* event, ULONG size) {
    std::vector<BYTE> schema(size);
    auto* info = reinterpret_cast<TRACE_EVENT_INFO*>(schema.data());
    if (TdhGetEventInformation(event, 0, nullptr, info, &size) != ERROR_SUCCESS) {
        return;
    }
    ++decoded;
    auto text_at = [&](ULONG offset) {
        return offset ? utf8(reinterpret_cast<wchar_t*>(schema.data() + offset)) : "";
    };
    output << ",\"provider_name\":" << quote(text_at(info->ProviderNameOffset))
           << ",\"task\":" << quote(text_at(info->TaskNameOffset))
           << ",\"opcode_name\":" << quote(text_at(info->OpcodeNameOffset)) << ",\"properties\":[";
    for (ULONG i = 0; i < info->TopLevelPropertyCount; ++i) {
        if (i) {
            output << ",";
        }
        write_property(event, schema, info->EventPropertyInfoArray[i]);
    }
    output << "]";
}

// One event of the trace: counted always, written when it belongs to a selected process.
void WINAPI record(EVENT_RECORD* event) {
    ++total;
    if (!selected.count(event->EventHeader.ProcessId)) {
        return;
    }
    ++kept;
    wchar_t guid[40]{};
    StringFromGUID2(event->EventHeader.ProviderId, guid, 40);
    providers[guid]++;
    output << "{\"pid\":" << event->EventHeader.ProcessId << ",\"tid\":" << event->EventHeader.ThreadId
           << ",\"timestamp\":" << event->EventHeader.TimeStamp.QuadPart << ",\"provider\":" << quote(utf8(guid))
           << ",\"opcode\":" << unsigned(event->EventHeader.EventDescriptor.Opcode)
           << ",\"version\":" << unsigned(event->EventHeader.EventDescriptor.Version)
           << ",\"event_id\":" << event->EventHeader.EventDescriptor.Id << ",\"flags\":" << event->EventHeader.Flags
           << ",\"payload\":" << quote(hex(event->UserData, event->UserDataLength));
    ULONG size = 0;
    ULONG status = TdhGetEventInformation(event, 0, nullptr, nullptr, &size);
    output << ",\"schema_status\":" << status;
    if (status == ERROR_INSUFFICIENT_BUFFER) {
        write_schema(event, size);
    }
    output << "}\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        return 2;
    }
    for (int i = 3; i < argc; ++i) {
        selected.insert(wcstoul(argv[i], nullptr, 10));
    }
    if (std::filesystem::exists(argv[2])) {
        return 2;
    }
    output.open(std::filesystem::path(argv[2]));
    if (!output) {
        return 2;
    }
    EVENT_TRACE_LOGFILEW input{};
    input.LogFileName = argv[1];
    input.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    input.EventRecordCallback = record;
    TRACEHANDLE handle = OpenTraceW(&input);
    if (handle == INVALID_PROCESSTRACE_HANDLE) {
        return 3;
    }
    ULONG result = ProcessTrace(&handle, 1, nullptr, nullptr);
    CloseTrace(handle);
    output.close();
    printf("{\"process_trace_status\":%lu,\"total_events\":%llu,\"selected_events\":%llu,\"decoded_events\":%llu,"
           "\"events_lost\":%lu,\"providers\":{",
           result, total, kept, decoded, input.EventsLost);
    bool first = true;
    for (auto& entry : providers) {
        if (!first) {
            printf(",");
        }
        first = false;
        printf("%s:%llu", quote(utf8(entry.first.c_str())).c_str(), entry.second);
    }
    printf("}}\n");
    return result == ERROR_SUCCESS ? 0 : 4;
}
