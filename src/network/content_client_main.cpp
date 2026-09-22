#include "peer.h"
#include <windows.h>
#include <iostream>
#include <string>

using namespace sporemp::network;
namespace {
int digit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;}
bool parse(const std::string& s,ContentWire& out) {
    if(s.size()!=out.size()*2)return false;
    for(size_t n=0;n<out.size();++n){const int a=digit(s[n*2]),b=digit(s[n*2+1]);if(a<0||b<0)return false;out[n]=uint8_t(a*16+b);}return true;
}
std::string text(const ContentWire& bytes){constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);for(auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;}
int fail(const char* code){std::cout<<"{\"event\":\"error\",\"code\":\""<<code<<"\"}\n"<<std::flush;return 2;}
}
int wmain(int argc,wchar_t** argv) {
    const bool fixture=argc==4&&std::wstring(argv[1])==L"--fixture-authority";
    const int config_arg=fixture?2:1;
    if((argc!=3&&!fixture)||std::wstring(argv[config_arg])!=L"--config")return fail("usage_config_path_required");
    const auto input=GetStdHandle(STD_INPUT_HANDLE);
    if(input==INVALID_HANDLE_VALUE||GetFileType(input)!=FILE_TYPE_PIPE)return fail("private_stdin_pipe_required");
    PeerConfig config;std::string error;
    if(!read_peer_config(argv[config_arg+1],config,error)||config.role!=(fixture?Role::authority:Role::player))return fail("invalid_role_configuration");
    Peer peer;if(!peer.start(config,error))return fail("network_start_failed");
    bool welcomed=false,done=false;std::string line;const auto deadline=GetTickCount64()+600000;
    while(!done&&GetTickCount64()<deadline) {
        Event incoming;size_t count=0;
        while(count++<4096&&peer.poll(incoming)) {
            if(incoming.kind==EventKind::disconnected){peer.stop();return fail("network_disconnected");}
            if(incoming.kind!=EventKind::packet)continue;
            const auto& packet=incoming.packet;
            if(packet.kind==Kind::welcome) {
                welcomed=true;std::cout<<"{\"event\":\"authenticated\",\"player\":"<<packet.player<<",\"session\":"<<packet.session<<",\"native_gameplay_client\":false}\n"<<std::flush;
            }else if(packet.kind==Kind::reject){peer.stop();return fail("session_rejected");}
            else if(packet.kind==Kind::content_response)std::cout<<"{\"event\":\"content\",\"payload\":\""<<text(packet.content)<<"\"}\n"<<std::flush;
            else if(packet.kind==Kind::scene_end)std::cout<<"{\"event\":\"scene_metadata\",\"scene\":"<<packet.scene<<",\"baseline\":"<<packet.baseline<<",\"entities\":"<<packet.count<<",\"native_baseline_applied\":false}\n"<<std::flush;
        }
        DWORD available=0;
        if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)) {
            if(GetLastError()==ERROR_BROKEN_PIPE){done=true;break;}peer.stop();return fail("stdin_unavailable");
        }
        const auto bounded=available<4096?available:4096;
        for(DWORD n=0;n<bounded;++n) {
            char ch=0;DWORD read=0;if(!ReadFile(input,&ch,1,&read,nullptr)||read!=1){done=true;break;}
            if(ch=='\r')continue;
            if(ch!='\n'){if(line.size()>=content_wire_bytes*2){peer.stop();return fail("stdin_frame_too_long");}line+=ch;continue;}
            if(line=="quit"){done=true;break;}
            ContentWire bytes;ContentFrame frame;
            if(!welcomed||!parse(line,bytes)||!decode_content(bytes,frame)||!frame.request||!content_request_op(frame.op)||
               (frame.failure!=ContentFailure::none&&frame.op!=ContentOp::dependency_failure)) {
                peer.stop();return fail("invalid_or_premature_content_request");
            }
            Packet packet;packet.kind=Kind::content_request;packet.content=bytes;
            if(!peer.send(packet)){peer.stop();return fail("output_queue_full");}line.clear();
        }
        Sleep(1);
    }
    peer.stop();if(!done)return fail("bounded_client_deadline");
    std::cout<<"{\"event\":\"closed\",\"clean\":true}\n"<<std::flush;return 0;
}
