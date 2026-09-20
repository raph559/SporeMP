// Developer-only process loopback capture. No input, injection or game mutation.
// API contract: https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/
// Uses the installed pinned Windows SDK; no downloaded runtime or audio library.
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <string>

using namespace Microsoft::WRL;
namespace {
void require(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        char message[160]; sprintf_s(message, "%s HRESULT=0x%08lx", operation, static_cast<unsigned long>(hr));
        throw std::runtime_error(message);
    }
}
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
class Activation final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IActivateAudioInterfaceCompletionHandler, FtmBase> {
public:
    Handle complete{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override {
        ComPtr<IUnknown> activated;
        HRESULT inner = E_UNEXPECTED;
        result = operation->GetActivateResult(&inner, &activated);
        if (SUCCEEDED(result)) result = inner;
        if (SUCCEEDED(result)) result = activated.As(&client);
        SetEvent(complete.value);
        return S_OK;
    }
};
void write(HANDLE file, const void* bytes, DWORD count) {
    DWORD written = 0;
    if (!WriteFile(file, bytes, count, &written, nullptr) || written != count)
        throw std::runtime_error("WAV write failed");
}
void wave(HANDLE file, const std::vector<BYTE>& bytes) {
    const DWORD size = static_cast<DWORD>(bytes.size()), riff_size = size + 36, fmt_size = 16;
    const WORD pcm = WAVE_FORMAT_PCM, channels = 2, bits = 16, align = 4;
    const DWORD rate = 48000, bytes_per_second = rate * align;
    write(file, "RIFF", 4); write(file, &riff_size, 4); write(file, "WAVEfmt ", 8);
    write(file, &fmt_size, 4); write(file, &pcm, 2); write(file, &channels, 2);
    write(file, &rate, 4); write(file, &bytes_per_second, 4);
    write(file, &align, 2); write(file, &bits, 2); write(file, "data", 4); write(file, &size, 4);
    if (size) write(file, bytes.data(), size);
    if (!FlushFileBuffers(file)) throw std::runtime_error("WAV flush failed");
}
unsigned number(const wchar_t* text, unsigned maximum) {
    wchar_t* end = nullptr; const auto value = wcstoul(text, &end, 10);
    if (!value || !end || *end || value > maximum) throw std::runtime_error("Invalid numeric argument");
    return value;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) { std::fprintf(stderr,"Usage: SporeMP.AudioCapture PID SECONDS NEW_LOCAL_WAV\n"); return 2; }
    try {
        const DWORD pid = number(argv[1], MAXDWORD);
        const unsigned seconds = number(argv[2], 60);
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
        wchar_t path[32768]{}; DWORD length = _countof(path);
        if (!process.value || !QueryFullProcessImageNameW(process.value, 0, path, &length) ||
            _wcsicmp(path, L"C:\\Games\\SPORE\\SporebinEP1\\SporeApp.exe"))
            throw std::runtime_error("Expected the explicitly selected original SPORE process");
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(process.value, &created, &exited, &kernel, &user))
            throw std::runtime_error("Process creation identity unavailable");
        require(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
        auto completion = Make<Activation>();
        if (!completion || !completion->complete.value) throw std::runtime_error("Activation event unavailable");
        AUDIOCLIENT_ACTIVATION_PARAMS parameters{};
        parameters.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        parameters.ProcessLoopbackParams.TargetProcessId = pid;
        parameters.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT variant{}; variant.vt = VT_BLOB;
        variant.blob.cbSize = sizeof(parameters); variant.blob.pBlobData = reinterpret_cast<BYTE*>(&parameters);
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        require(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
            &variant, completion.Get(), &operation), "ActivateAudioInterfaceAsync");
        if (WaitForSingleObject(completion->complete.value, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("Process loopback activation timed out");
        require(completion->result, "Process loopback activation");
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
        format.nSamplesPerSec = 48000; format.wBitsPerSample = 16;
        format.nBlockAlign = 4; format.nAvgBytesPerSec = 192000;
        auto client = completion->client;
        require(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            0, 0, &format, nullptr), "Initialize process capture");
        Handle samples(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!samples.value) throw std::runtime_error("Sample event unavailable");
        require(client->SetEventHandle(samples.value), "SetEventHandle");
        ComPtr<IAudioCaptureClient> capture;
        require(client->GetService(IID_PPV_ARGS(&capture)), "Get capture service");
        Handle output(CreateFileW(argv[3], GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (output.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Output must be a new writable WAV file");
        std::vector<BYTE> bytes;
        const size_t maximum = size_t(seconds) * format.nAvgBytesPerSec;
        bytes.reserve(maximum);
        unsigned packets = 0, discontinuities = 0, timestamp_errors = 0, silent_packets = 0;
        UINT64 first_qpc = 0, last_qpc = 0;
        require(client->Start(), "Start");
        const auto started = GetTickCount64();
        while (GetTickCount64() - started < seconds * 1000ULL && bytes.size() < maximum) {
            if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) break;
            const DWORD ready = WaitForSingleObject(samples.value, 1000);
            if (ready == WAIT_TIMEOUT) continue;
            if (ready != WAIT_OBJECT_0) throw std::runtime_error("Capture event failed");
            UINT32 available = 0;
            require(capture->GetNextPacketSize(&available), "GetNextPacketSize");
            while (available) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0; UINT64 position = 0, qpc = 0;
                require(capture->GetBuffer(&data, &frames, &flags, &position, &qpc), "GetBuffer");
                const size_t wanted = size_t(frames) * format.nBlockAlign;
                const size_t count = (wanted < maximum - bytes.size()) ? wanted : maximum - bytes.size();
                const auto offset = bytes.size(); bytes.resize(offset + count);
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) { ++silent_packets; std::memset(bytes.data() + offset, 0, count); }
                else if (count) std::memcpy(bytes.data() + offset, data, count);
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++discontinuities;
                if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ++timestamp_errors;
                if (!packets) first_qpc = qpc;
                last_qpc = qpc; ++packets;
                require(capture->ReleaseBuffer(frames), "ReleaseBuffer");
                require(capture->GetNextPacketSize(&available), "GetNextPacketSize");
            }
        }
        require(client->Stop(), "Stop");
        wave(output.value, bytes);
        std::printf("{\"schema_version\":1,\"process_id\":%lu,\"process_created_filetime\":%llu,"
            "\"capture_mode\":\"include_target_process_tree\",\"rate\":48000,\"channels\":2,\"bits\":16,"
            "\"frames\":%llu,\"packets\":%u,\"discontinuities\":%u,\"timestamp_errors\":%u,\"silent_packets\":%u,"
            "\"first_packet_qpc_100ns\":%llu,\"last_packet_qpc_100ns\":%llu,\"wall_ms\":%llu,"
            "\"listening_acceptance\":\"NOT_VERIFIED\"}\n", pid,
            (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime,
            static_cast<unsigned long long>(bytes.size() / format.nBlockAlign), packets, discontinuities,
            timestamp_errors, silent_packets, first_qpc, last_qpc, GetTickCount64() - started);
        return bytes.empty() ? 3 : 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
