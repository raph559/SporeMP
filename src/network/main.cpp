#include "coordinator.h"
#include <windows.h>
#include <sddl.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

using namespace sporemp::network;
namespace {
std::atomic<bool> quitting{false};
BOOL WINAPI ctrl(DWORD code){if(code==CTRL_C_EVENT||code==CTRL_BREAK_EVENT||code==CTRL_CLOSE_EVENT){quitting=true;return TRUE;}return FALSE;}
std::string quote(const std::string& s){std::string r="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){r+='\\';r+=char(c);}else if(c>=32&&c<127)r+=char(c);else r+='?';}return r+'"';}
int fail(const std::string& e){std::cout<<"{\"error\":"<<quote(e)<<"}\n";return 20;}
std::string content_log(const Packet& packet) {
    if(packet.kind!=Kind::content_request&&packet.kind!=Kind::content_response)return {};
    ContentFrame frame;
    if(!decode_content(packet.content,frame))return ",\"content_valid\":false";
    return ",\"content_valid\":true,\"content_op\":"+std::to_string(uint32_t(frame.op))+
        ",\"content_request\":"+std::to_string(frame.request)+",\"transaction\":"+std::to_string(frame.transaction)+
        ",\"content_error\":"+quote(content_failure_name(frame.failure))+",\"content_identity\":"+quote(hex(frame.identity))+
        ",\"content_offset\":"+std::to_string(frame.offset)+",\"content_total\":"+std::to_string(frame.total)+
        ",\"missing_group\":"+std::to_string(frame.missing.group)+",\"missing_instance\":"+std::to_string(frame.missing.instance)+
        ",\"missing_type\":"+std::to_string(frame.missing.type)+",\"world_index\":"+std::to_string(frame.world_index);
}
bool private_directory(const std::wstring& path,std::string& error){HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){error="open_token_failed";return false;}DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<uint8_t> storage(size);bool got=GetTokenInformation(token,TokenUser,storage.data(),size,&size)!=FALSE;CloseHandle(token);if(!got){error="token_identity_failed";return false;}LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid,&sid)){error="sid_conversion_failed";return false;}std::wstring sddl=L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;"+std::wstring(sid)+L")";LocalFree(sid);PSECURITY_DESCRIPTOR descriptor=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)){error="private_acl_failed";return false;}SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};bool ok=CreateDirectoryW(path.c_str(),&attributes)!=FALSE;DWORD last=GetLastError();LocalFree(descriptor);if(!ok){error="output_directory_must_be_new:"+std::to_string(last);return false;}return true;}
bool write_peer(const std::filesystem::path& path,const PeerConfig& c){std::ofstream o(path,std::ios::binary);o<<"schema=2\nhost="<<c.host<<"\nport="<<c.port<<"\ncertificate_sha256="<<hex(c.certificate_pin)<<"\ncredential="<<hex(c.credential)<<"\nrole="<<(c.role==Role::authority?"authority":"player")<<"\nbuild_sha256="<<hex(c.identity.build)<<"\nexecutable_sha256="<<hex(c.identity.executable)<<"\ncontent_sha256="<<hex(c.identity.content)<<"\nfixture_sha256="<<hex(c.identity.fixture)<<'\n';for(size_t index=0;index<world_file_count;++index)o<<world_files[index].field<<'='<<hex(c.identity.world[index])<<'\n';o.close();return bool(o);}
bool write_invite(const std::filesystem::path& path,const PeerConfig& c){std::ofstream o(path,std::ios::binary);o<<"sporemp://join?host="<<c.host<<"&port="<<c.port<<"&cert="<<hex(c.certificate_pin)<<"&token="<<hex(c.credential)<<'\n';o.close();return bool(o);}
bool read_server(const std::wstring& path,CoordinatorConfig& c,std::string& error){std::error_code ec;if(std::filesystem::file_size(path,ec)>4096||ec){error="server_config_missing_or_oversized";return false;}std::ifstream i{std::filesystem::path(path)};std::map<std::string,std::string> v;std::string l;while(std::getline(i,l)){if(!l.empty()&&l.back()=='\r')l.pop_back();auto eq=l.find('=');if(eq==std::string::npos||!v.emplace(l.substr(0,eq),l.substr(eq+1)).second){error="invalid_server_config";return false;}}const char* required[]{"schema","host","port","build_sha256","executable_sha256","content_sha256","fixture_sha256"};if(v.size()!=7+world_file_count){error="server_config_fields";return false;}for(auto name:required)if(!v.count(name)){error="server_config_fields";return false;}if(v["schema"]!="2"){error="server_schema";return false;}auto p=v["port"];if(p.empty()||p.size()>5||p.find_first_not_of("0123456789")!=std::string::npos){error="server_port";return false;}auto n=std::stoul(p);if(n>65535){error="server_port";return false;}c.port=static_cast<uint16_t>(n);c.bind_host=v["host"];if(!parse_hex(v["build_sha256"],c.session.identity.build)||!parse_hex(v["executable_sha256"],c.session.identity.executable)||!parse_hex(v["content_sha256"],c.session.identity.content)||!parse_hex(v["fixture_sha256"],c.session.identity.fixture)){error="server_identity";return false;}Digest zero{};if(c.session.identity.build==zero||c.session.identity.executable==zero||c.session.identity.content==zero||c.session.identity.fixture==zero){error="server_empty_identity";return false;}for(size_t index=0;index<world_file_count;++index){const auto field=world_files[index].field;if(!v.count(field)||!parse_hex(v[field],c.session.identity.world[index])||c.session.identity.world[index]==Digest{}){error=std::string("missing_or_invalid_world_identity:")+world_files[index].path;return false;}}if(c.session.identity.fixture!=c.session.identity.world[0]){error="world_fixture_identity_disagrees";return false;}return true;}
}
int wmain(int argc,wchar_t** argv){
    if(argc==4&&std::wstring(argv[1])==L"--world-identity"&&std::wstring(argv[2])==L"--root") {
        WorldIdentity world;std::string error;
        if(!read_world_identity(argv[3],world,error))return fail(error);
        std::cout<<"{\"schema\":1,\"evidence_class\":\"HOST\",\"native_validation\":\"NOT_RUN\",\"fields\":{";
        for(size_t index=0;index<world_file_count;++index){if(index)std::cout<<',';std::cout<<quote(world_files[index].field)<<':'<<quote(hex(world[index]));}
        std::cout<<"}}\n";return 0;
    }
    if(argc<4)return fail("usage: --serve --config PATH --output NEW_DIRECTORY | --client-probe --config PATH");
    std::wstring mode=argv[1],config_path,output_path;for(int n=2;n<argc;++n){std::wstring arg=argv[n];if((arg==L"--config"||arg==L"--output")&&n+1<argc){if(arg==L"--config")config_path=argv[++n];else output_path=argv[++n];}else return fail("unknown_or_missing_argument");}
    std::string error;
    if(mode==L"--client-probe"){
        PeerConfig config;if(!read_peer_config(config_path,config,error))return fail(error);if(config.role!=Role::player)return fail("probe_requires_player_credential");Peer peer;if(!peer.start(config,error))return fail(error);auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);Event e;
        while(std::chrono::steady_clock::now()<deadline){while(peer.poll(e)){if(e.kind==EventKind::disconnected){peer.stop();return fail(e.detail);}if(e.kind==EventKind::packet&&e.packet.kind==Kind::reject){peer.stop();return fail(error_detail(e.packet));}if(e.kind==EventKind::packet&&e.packet.kind==Kind::welcome){peer.stop();std::cout<<"{\"authenticated\":true,\"player\":"<<e.packet.player<<",\"session\":"<<e.packet.session<<",\"protocol\":1,\"tls\":\"1.2\",\"native_baseline_applied\":false}\n";return 0;}}std::this_thread::sleep_for(std::chrono::milliseconds(2));}peer.stop();return fail("authentication_timeout");
    }
    if(mode!=L"--serve"||output_path.empty())return fail("invalid_server_arguments");CoordinatorConfig config;if(!read_server(config_path,config,error))return fail(error);if(!private_directory(output_path,error))return fail(error);Coordinator server;if(!server.start(config,error))return fail(error);std::filesystem::path directory(output_path);
    if(!write_peer(directory/L"authority.conf",server.peer_config(Role::authority))||!write_peer(directory/L"player-1.conf",server.peer_config(Role::player,1))||!write_peer(directory/L"player-2.conf",server.peer_config(Role::player,2))||!write_invite(directory/L"player-1.invite",server.peer_config(Role::player,1))||!write_invite(directory/L"player-2.invite",server.peer_config(Role::player,2)))return fail("write_private_invite_failed");
    auto player=server.peer_config(Role::player,1);std::ofstream log(directory/L"coordinator.jsonl",std::ios::binary);if(!log)return fail("coordinator_log_failed");SetConsoleCtrlHandler(ctrl,TRUE);std::cout<<"{\"ready\":true,\"port\":"<<player.port<<",\"certificate_sha256\":"<<quote(hex(player.certificate_pin))<<"}\n"<<std::flush;
    CoordinatorEvent e;uint64_t events=0;bool exhausted=false;ULONGLONG next_stop_check=0,next_log_flush=0;while(!quitting){size_t count=0;while(count++<8192&&server.poll(e)){++events;log<<"{\"time_ms\":"<<GetTickCount64()<<",\"connection\":"<<e.connection<<",\"event\":"<<int(e.event.kind)<<",\"kind\":"<<uint32_t(e.event.packet.kind)<<",\"sequence\":"<<e.event.packet.sequence<<",\"scene\":"<<e.event.packet.scene<<",\"baseline\":"<<e.event.packet.baseline<<",\"player\":"<<e.event.packet.player<<",\"entity\":"<<e.event.packet.entity.id<<",\"generation\":"<<e.event.packet.entity.generation<<",\"tick\":"<<e.event.packet.entity.tick<<",\"outbound\":"<<(e.outbound?"true":"false")<<",\"request\":"<<e.event.packet.request<<",\"target\":"<<e.event.packet.target<<",\"target_generation\":"<<e.event.packet.target_generation<<",\"verb\":"<<uint32_t(e.event.packet.verb)<<",\"life_state\":"<<e.event.packet.entity.life_state<<",\"error\":"<<quote(error_detail(e.event.packet))<<",\"detail\":"<<quote(e.event.detail)<<content_log(e.event.packet)<<"}\n";if(events%128==0)log.flush();if(!log||events>=10000000){quitting=true;exhausted=true;break;}}auto now=GetTickCount64();if(now>=next_log_flush){log.flush();next_log_flush=now+100;}if(now>=next_stop_check){next_stop_check=now+200;std::error_code ec;if(std::filesystem::is_regular_file(directory/L"stop.request",ec))quitting=true;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}server.stop();log<<"{\"stopped\":true,\"events\":"<<events<<",\"log_limit_exhausted\":"<<(exhausted?"true":"false")<<"}\n";log.flush();return log&&!exhausted?0:21;
}
