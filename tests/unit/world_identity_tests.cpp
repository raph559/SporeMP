#include "protocol.h"
#include "peer.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace sporemp::network;
namespace {
int assertions=0;
void check(bool pass,const char* message){++assertions;if(!pass)throw std::runtime_error(message);}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        (L"sporemp-world-host-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Temporary(){if(!std::filesystem::create_directory(path))throw std::runtime_error("fresh world test directory required");}
    ~Temporary(){std::error_code error;std::filesystem::remove_all(path,error);}
};
void write(const std::filesystem::path& path,const std::string& value){std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream<<value;}
}
int world_identity_tests(){
    Temporary temporary;
    WorldIdentity world{},before{};std::string error;
    check(!read_world_identity(temporary.path.wstring(),world,error)&&error=="canonical_world_missing_or_unreadable:Games/Game0/Satiria.spo","missing exact first file denies world admission");
    for(const auto& entry:world_files)write(temporary.path/entry.path,"abc");
    check(read_world_identity(temporary.path.wstring(),world,error)&&valid_world_identity(world),"six closed original-path fixture files inspected");
    for(const auto& value:world)check(hex(value)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","actual sha256 known vector for each file");
    before=world;
    for(size_t index=0;index<world_file_count;++index){
        auto path=temporary.path/world_files[index].path;
        write(path,"abd");
        check(read_world_identity(temporary.path.wstring(),world,error)&&world[index]!=before[index],"same name and size changed bytes change world identity");
        for(size_t other=0;other<world_file_count;++other)if(other!=index)check(world[other]==before[other],"unmodified file identity retained");
        std::filesystem::remove(path);
        check(!read_world_identity(temporary.path.wstring(),world,error)&&error==std::string("canonical_world_missing_or_unreadable:")+world_files[index].path,"each missing file precisely identified");
        write(path,"abc");
    }
    auto path=temporary.path/world_files[1].path;
    HANDLE writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    check(writer!=INVALID_HANDLE_VALUE,"writer test handle opened");
    const bool accepted=read_world_identity(temporary.path.wstring(),world,error);CloseHandle(writer);
    check(!accepted&&error=="canonical_world_not_closed_or_readable:Games/Game0/planetRecords.pkp","open writer denies quiescent world hash");
    std::filesystem::resize_file(path,128ull*1024*1024+1);
    check(!read_world_identity(temporary.path.wstring(),world,error)&&error=="canonical_world_size_limit:Games/Game0/planetRecords.pkp","oversized world fails before hashing");
    write(path,"abc");
    std::filesystem::remove(path);std::filesystem::create_directory(path);
    check(!read_world_identity(temporary.path.wstring(),world,error)&&error=="canonical_world_unsafe_path:Games/Game0/planetRecords.pkp","directory cannot stand in for world file");
    std::filesystem::remove(path);write(path,"abc");
    Packet rejection;rejection.kind=Kind::reject;rejection.error=Error::world_mismatch;
    for(size_t index=0;index<world_file_count;++index){
        rejection.world_index=static_cast<uint32_t>(index);const auto wire=encode(rejection);Packet decoded;
        check(decode(wire.data(),wire.size(),decoded,error)&&error_detail(decoded)==std::string("canonical_world_mismatch:")+world_files[index].path,"wire rejection preserves allowlisted diagnostic without arbitrary path strings");
    }
    rejection.world_index=static_cast<uint32_t>(world_file_count);auto wire=encode(rejection);Packet decoded;
    check(!decode(wire.data(),wire.size(),decoded,error)&&error=="invalid_world_mismatch_index","out of range world path index refused");
    rejection.world_index=1;rejection.error=Error::ownership;wire=encode(rejection);
    check(!decode(wire.data(),wire.size(),decoded,error),"world path index cannot accompany unrelated error");
    rejection={};wire=encode(rejection);wire[596]=1;
    check(!decode(wire.data(),wire.size(),decoded,error)&&error=="nonzero_reserved","schema5 tail remains reserved");
    return assertions;
}
