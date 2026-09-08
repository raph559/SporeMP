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
unsigned long long total=0,kept=0,decoded=0;
std::map<std::wstring,unsigned long long> providers;
std::string utf8(const wchar_t* value) {
    if(!value)return {};
    int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    if(n<1)return {};
    std::string result(n,'\0'); WideCharToMultiByte(CP_UTF8,0,value,-1,result.data(),n,nullptr,nullptr); result.pop_back();return result;
}
std::string quote(const std::string& value) {
    std::string result="\"";
    for(unsigned char c:value) {
        if(c=='\\'||c=='"') {result+='\\';result+=static_cast<char>(c);}
        else if(c<32) {char b[7];sprintf_s(b,"\\u%04x",c);result+=b;}
        else result+=static_cast<char>(c);
    }
    return result+'"';
}
std::string hex(const void* ptr,size_t length) {
    const auto* b=static_cast<const unsigned char*>(ptr); constexpr char table[]="0123456789abcdef";
    std::string s; s.reserve(length*2);
    for(size_t i=0;i<length;++i) {s+=table[b[i]>>4];s+=table[b[i]&15];} return s;
}
void WINAPI record(EVENT_RECORD* event) {
    ++total;
    if(!selected.count(event->EventHeader.ProcessId))return;
    ++kept;
    wchar_t guid[40]{};StringFromGUID2(event->EventHeader.ProviderId,guid,40);providers[guid]++;
    output<<"{\"pid\":"<<event->EventHeader.ProcessId<<",\"tid\":"<<event->EventHeader.ThreadId
          <<",\"timestamp\":"<<event->EventHeader.TimeStamp.QuadPart<<",\"provider\":"<<quote(utf8(guid))
          <<",\"opcode\":"<<unsigned(event->EventHeader.EventDescriptor.Opcode)<<",\"version\":"<<unsigned(event->EventHeader.EventDescriptor.Version)
          <<",\"event_id\":"<<event->EventHeader.EventDescriptor.Id<<",\"flags\":"<<event->EventHeader.Flags<<",\"payload\":"<<quote(hex(event->UserData,event->UserDataLength));
    ULONG size=0; ULONG status=TdhGetEventInformation(event,0,nullptr,nullptr,&size);
    output<<",\"schema_status\":"<<status;
    if(status==ERROR_INSUFFICIENT_BUFFER) {
        std::vector<BYTE> bytes(size);auto* info=reinterpret_cast<TRACE_EVENT_INFO*>(bytes.data());
        if(TdhGetEventInformation(event,0,nullptr,info,&size)==ERROR_SUCCESS) {
            ++decoded;
            auto str=[&](ULONG offset){return offset ? utf8(reinterpret_cast<wchar_t*>(bytes.data()+offset)) : "";};
            output<<",\"provider_name\":"<<quote(str(info->ProviderNameOffset))<<",\"task\":"<<quote(str(info->TaskNameOffset))<<",\"opcode_name\":"<<quote(str(info->OpcodeNameOffset))<<",\"properties\":[";
            for(ULONG i=0;i<info->TopLevelPropertyCount;++i) {
                if(i)output<<",";
                auto& property=info->EventPropertyInfoArray[i];
                auto* name=reinterpret_cast<wchar_t*>(bytes.data()+property.NameOffset);
                PROPERTY_DATA_DESCRIPTOR descriptor{};descriptor.PropertyName=reinterpret_cast<ULONGLONG>(name);descriptor.ArrayIndex=ULONG_MAX;
                ULONG length=0; auto error=TdhGetPropertySize(event,0,nullptr,1,&descriptor,&length);
                output<<"{\"name\":"<<quote(utf8(name))<<",\"flags\":"<<property.Flags<<",\"in_type\":"<<property.nonStructType.InType<<",\"error\":"<<error;
                if(error==ERROR_SUCCESS && length<1024*1024) {
                    std::vector<BYTE> value(length+2);
                    if(TdhGetProperty(event,0,nullptr,1,&descriptor,length,value.data())==ERROR_SUCCESS) {
                        output<<",\"hex\":"<<quote(hex(value.data(),length));
                        if(property.Flags==0 && property.nonStructType.InType==TDH_INTYPE_UNICODESTRING)output<<",\"text\":"<<quote(utf8(reinterpret_cast<wchar_t*>(value.data())));
                        if(property.Flags==0 && property.nonStructType.InType==TDH_INTYPE_ANSISTRING)output<<",\"text\":"<<quote(reinterpret_cast<char*>(value.data()));
                    }
                }
                output<<"}";
            }
            output<<"]";
        }
    }
    output<<"}\n";
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc<4)return 2;
    for(int i=3;i<argc;++i)selected.insert(wcstoul(argv[i],nullptr,10));
    if(std::filesystem::exists(argv[2]))return 2;
    output.open(std::filesystem::path(argv[2]));if(!output)return 2;
    EVENT_TRACE_LOGFILEW input{};input.LogFileName=argv[1];input.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;input.EventRecordCallback=record;
    TRACEHANDLE handle=OpenTraceW(&input);if(handle==INVALID_PROCESSTRACE_HANDLE)return 3;
    ULONG result=ProcessTrace(&handle,1,nullptr,nullptr);CloseTrace(handle);output.close();
    printf("{\"process_trace_status\":%lu,\"total_events\":%llu,\"selected_events\":%llu,\"decoded_events\":%llu,\"events_lost\":%lu,\"providers\":{",result,total,kept,decoded,input.EventsLost);
    bool first=true;
    for(auto& entry:providers) {if(!first)printf(",");first=false;printf("%s:%llu",quote(utf8(entry.first.c_str())).c_str(),entry.second);}
    printf("}}\n");return result==ERROR_SUCCESS?0:4;
}
