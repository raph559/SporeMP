#include "native_content_abi.h"
#include "content_png.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
struct Key { uint32_t instance, type, group; };
struct Data {
    uint32_t sentinel = 0xa13e92b7;
    int releases = 0;
    Data* last_receiver = nullptr;
    __declspec(noinline) int release() { last_receiver = this; ++releases; return -37; }
};
using Abi = sporemp::NativeContentAbi<Key, Data>;
using Lookup = sporemp::NativeContentLookupAbi<Key>;
struct Database {
    const char16_t* path = u"C:\\fixture\\Parts.package";
    __declspec(noinline) const char16_t* location() { return path; }
};
struct Manager {
    const Key* key = nullptr;
    Key* output = nullptr;
    void* selected = nullptr;
    Database database;
    uint32_t calls = 0;
    __declspec(noinline) void* exact(const Key& value) {
        key = &value; ++calls;
        return value.type == 0x00b1b104 ? &database : nullptr;
    }
    __declspec(noinline) void* mapped(const Key& value, Key* result, void* source) {
        key = &value; output = result; selected = source; ++calls;
        if (result) *result = {value.instance, 0x00b1b104, value.group};
        return source ? source : &database;
    }
};
Manager lookup_fixture;
__declspec(noinline) void* __cdecl get_fixture() { return &lookup_fixture; }
struct Importer {
    const char16_t* path = nullptr;
    Key* output = nullptr;
    uint32_t calls = 0;
    __declspec(noinline) bool invoke(const char16_t* file, Key& key) {
        path = file; output = &key; ++calls;
        key = {0xfedcba98, 0x2b978c46, 0x40626200};
        return file != nullptr;
    }
};
Key* observed_key = nullptr;
Data** observed_output = nullptr;
Data fixture;
int loads = 0;
__declspec(noinline) bool __cdecl load_fixture(Key* key, Data** output) {
    observed_key = key; observed_output = output; ++loads;
    if (output) *output = key ? &fixture : nullptr;
    return key && key->instance == 0xfedcba98 && key->type == 0x2b978c46 && key->group == 0x81234567;
}
template<class Function, class Member> Function address(Member member) {
    Function function{};
    static_assert(sizeof(function) == sizeof(member), "Win32 member pointer representation");
    std::memcpy(&function, &member, sizeof(function));
    return function;
}
}

int main() {
    static_assert(sizeof(void*) == 4, "HOST ABI fixture requires Win32");
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::printf("%s: %s (HOST CONTENT ABI; SPORE NOT LOADED)\n", ok ? "PASS" : "FAIL", text);
        if (!ok) ++failures;
    };
    Abi::Load volatile load = &load_fixture;
    Abi::Release volatile release = address<Abi::Release>(&Data::release);
    Key key{0xfedcba98, 0x2b978c46, 0x81234567};
    Data* output = nullptr;
    check(load(&key, &output) && observed_key == &key && observed_output == &output && output == &fixture,
        "cdecl keeps two pointer slots, unsigned key fields and true AL");
    check(release(output) == -37 && fixture.releases == 1 && fixture.last_receiver == &fixture,
        "thiscall receives the owned result in ECX and preserves signed EAX");
    output = &fixture;
    check(!load(nullptr, &output) && !output && !observed_key && observed_output == &output,
        "false native result can preserve a null output without an owned release");
    uintptr_t before = 0, after = 0;
    __asm mov before, esp
    for (int i = 0; i < 4096; ++i) { load(&key, &output); release(output); }
    __asm mov after, esp
    check(before == after && loads == 4098 && fixture.releases == 4097 && fixture.sentinel == 0xa13e92b7,
        "4096 load/release pairs balance stack and release exactly one reference each");

    Lookup::Get volatile get = &get_fixture;
    Lookup::FindDatabase volatile exact = address<Lookup::FindDatabase>(&Manager::exact);
    Lookup::FindRecord volatile mapped = address<Lookup::FindRecord>(&Manager::mapped);
    Lookup::GetLocation volatile location = address<Lookup::GetLocation>(&Database::location);
    Key destination{};
    check(get() == &lookup_fixture && !exact(get(), key) && lookup_fixture.key == &key,
        "lookup getter and thiscall exact miss preserve receiver, reference and null result");
    check(mapped(get(), key, &destination, nullptr) == &lookup_fixture.database &&
        lookup_fixture.key == &key && lookup_fixture.output == &destination && !lookup_fixture.selected &&
        destination.instance == key.instance && destination.type == 0x00b1b104 && destination.group == key.group,
        "mapped lookup passes key, output and null database in three distinct stack slots");
    Database alternate;
    check(mapped(get(), key, nullptr, &alternate) == &alternate && !lookup_fixture.output &&
        lookup_fixture.selected == &alternate && location(&alternate) == alternate.path,
        "explicit database and borrowed UTF-16 return preserve their identities without ownership operations");
    __asm mov before, esp
    for (int i = 0; i < 4096; ++i) { exact(get(), destination); mapped(get(), key, &destination, nullptr); location(&alternate); }
    __asm mov after, esp
    check(before == after && lookup_fixture.calls == 8195,
        "4096 native-shaped lookup triples balance RET 4, RET 12 and plain RET stack conventions");
    sporemp::NativeContentImportAbi<Key>::Import volatile import = address<sporemp::NativeContentImportAbi<Key>::Import>(&Importer::invoke);
    Importer importer;
    const char16_t import_path[] = u"C:\\fixture\\m08-import.png";
    check(import(&importer, import_path, destination) && importer.path == import_path && importer.output == &destination &&
        destination.instance == 0xfedcba98 && destination.type == 0x2b978c46 && destination.group == 0x40626200,
        "import receives ECX, UTF-16 path, key reference and true AL with unsigned output fields");
    check(!import(&importer, nullptr, destination) && destination.instance == 0xfedcba98,
        "false import result can still return an existing native key and must not imply creation");
    __asm mov before, esp
    for (int i = 0; i < 4096; ++i) import(&importer, import_path, destination);
    __asm mov after, esp
    check(before == after && importer.calls == 4098, "4096 native-shaped imports balance RET 8 without retaining a native object");

    // Independent Python struct/zlib-generated transparent RGBA8 1x1 PNG.
    // Container validity is not native Model-in-Picture acceptance.
    const uint8_t png[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x60,0x00,0x02,0x00,0x00,0x05,0x00,0x01,0x7a,0x5e,0xab,0x3f,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
    check(!sporemp::creation_png_envelope(png, sizeof(png)), "complete RGBA8 container is supported without claiming native payload validation");
    check(sporemp::creation_png_envelope(nullptr, sizeof(png)) != nullptr, "null PNG input is rejected without dereference");
    for (size_t i = 0; i < sizeof(png); ++i) {
        check(sporemp::creation_png_envelope(png, i) != nullptr, "every PNG truncation is rejected before native import");
        std::vector<uint8_t> corrupt(png, png + sizeof(png)); corrupt[i] ^= 1;
        check(sporemp::creation_png_envelope(corrupt.data(), corrupt.size()) != nullptr, "every single-byte envelope mutation is rejected by signature, bounds or CRC");
    }
    std::vector<uint8_t> oversized(sporemp::max_creation_png_bytes + 1);
    check(sporemp::creation_png_envelope(oversized.data(), oversized.size()) != nullptr, "oversized PNG rejected before scanning or native allocation");
    std::vector<uint8_t> trailing(png, png + sizeof(png)); trailing.push_back(0);
    check(sporemp::creation_png_envelope(trailing.data(), trailing.size()) != nullptr, "valid IEND cannot hide trailing content");
    std::vector<uint8_t> duplicate(png, png + 33); duplicate.insert(duplicate.end(), png + 8, png + sizeof(png));
    check(sporemp::creation_png_envelope(duplicate.data(), duplicate.size()) != nullptr, "duplicate CRC-valid IHDR cannot be accepted as payload");
    struct HeaderCase { uint8_t bytes[13]; uint32_t crc; };
    const HeaderCase headers[] = {
        {{0,0,0,0,0,0,0,1,8,6,0,0,0},0xf0d7afb7},
        {{0,0,2,1,0,0,0,1,8,6,0,0,0},0x47797d48},
        {{0,0,0,1,0,0,0,1,16,6,0,0,0},0x4f8518ca},
        {{0,0,0,1,0,0,0,1,8,2,0,0,0},0x907753de},
        {{0,0,0,1,0,0,0,1,8,6,0,0,1},0x6812f41f}
    };
    for (const auto& header : headers) {
        std::vector<uint8_t> changed(png,png+sizeof(png));
        std::memcpy(changed.data()+16,header.bytes,13);
        for (size_t i=0;i<4;++i) changed[29+i]=static_cast<uint8_t>(header.crc>>(24-i*8));
        const auto error=sporemp::creation_png_envelope(changed.data(),changed.size());
        check(error && !std::strcmp(error,"png_format_unqualified"), "CRC-valid zero/oversized dimensions, depth, color and interlace fail the qualified format gate");
    }

    struct SpanCase { sporemp::NativeContentSpan span; uint32_t stride, limit, expected; bool valid; };
    const SpanCase spans[] = {
        {{0,0,0}, 4, 512, 0, true},
        {{0x1000,0x1010,0x1020}, 4, 512, 4, true},
        {{0x1000,0x1000,0x1004}, 4, 512, 0, true},
        {{0,4,4}, 4, 512, 0, false},
        {{0x1010,0x1000,0x1020}, 4, 512, 0, false},
        {{0x1000,0x1010,0x1008}, 4, 512, 0, false},
        {{0x1000,0x1011,0x1020}, 4, 512, 0, false},
        {{0x1000,0x1010,0x1021}, 4, 512, 0, false},
        {{0x1000,0x1010,0x1020}, 4, 3, 0, false},
        {{0x1000,0x1010,0x1020}, 0, 512, 0, false},
        {{0xfffffff0,0xffffffff,0xffffffff}, 1, 15, 15, true},
        {{0xfffffff0,0x10,0xffffffff}, 1, 512, 0, false},
    };
    for (const auto& value : spans) {
        uint32_t count = UINT32_MAX;
        check(sporemp::native_content_span_count(value.span,value.stride,value.limit,count) == value.valid && count == value.expected,
            "vector integer bounds reject malformed, wrapped, truncated and oversized spans");
    }
    check(sporemp::native_content_capability_range(0,0,0) && sporemp::native_content_capability_range(3,2,5) &&
        sporemp::native_content_capability_range(5,0,5), "empty and end-aligned native capability slices are bounded");
    check(!sporemp::native_content_capability_range(-1,0,5) && !sporemp::native_content_capability_range(0,-1,5) &&
        !sporemp::native_content_capability_range(4,2,5) && !sporemp::native_content_capability_range(6,0,5),
        "negative sentinels and capability slices past the vector are rejected explicitly");
    return failures ? 1 : 0;
}
