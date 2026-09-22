#include "peer.h"
#include "channel.h"
#include <filesystem>
#include <fstream>
#include <map>

namespace sporemp::network {
struct Peer::Impl {std::unique_ptr<detail::Channel> channel;};
Peer::Peer():impl_(std::make_unique<Impl>()){}
Peer::~Peer(){stop();}
bool Peer::start(const PeerConfig& c,std::string& e){if(impl_->channel){e="peer_already_started";return false;}if(c.host.empty()||c.host.size()>253||!c.port){e="invalid_endpoint";return false;}if(!valid_world_identity(c.identity.world)){e="missing_world_identity";return false;}if(!detail::initialize_sockets(e))return false;impl_->channel=std::make_unique<detail::Channel>();impl_->channel->start_client(c);return true;}
bool Peer::try_restart(const PeerConfig& c,std::string& e){if(impl_->channel){if(!impl_->channel->exited()){e="network_thread_still_exiting";return false;}impl_->channel.reset();}return start(c,e);}
bool Peer::send(Packet p){return impl_->channel&&impl_->channel->send(std::move(p));}
bool Peer::poll(Event& e){return impl_->channel&&impl_->channel->poll(e);}
void Peer::stop(){if(impl_->channel){impl_->channel->stop();impl_->channel.reset();}}
bool read_peer_config(const std::wstring& path,PeerConfig& result,std::string& error){
    std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size>4096){error="config_missing_or_oversized";return false;}std::ifstream input{std::filesystem::path(path)};std::map<std::string,std::string> values;std::string line;
    while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();auto eq=line.find('=');if(eq==std::string::npos||eq==0||!values.emplace(line.substr(0,eq),line.substr(eq+1)).second){error="invalid_or_duplicate_config_field";return false;}}
    const char* required[]{"schema","host","port","certificate_sha256","credential","role","build_sha256","executable_sha256","content_sha256","fixture_sha256"};if(values.size()!=10+world_file_count){error="unexpected_config_fields";return false;}for(auto key:required)if(!values.count(key)){error="missing_config_field";return false;}
    PeerConfig c;if(values["schema"]!="2"||(values["role"]!="player"&&values["role"]!="authority")){error="unsupported_config_schema_or_role";return false;}
    c.host=values["host"];if(c.host.empty()||c.host.size()>253||c.host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:")!=std::string::npos){error="invalid_host";return false;}
    const auto& port=values["port"];if(port.empty()||port.size()>5||port.find_first_not_of("0123456789")!=std::string::npos){error="invalid_port";return false;}unsigned number=static_cast<unsigned>(std::stoul(port));if(!number||number>65535){error="invalid_port";return false;}c.port=static_cast<uint16_t>(number);c.role=values["role"]=="authority"?Role::authority:Role::player;
    if(!parse_hex(values["certificate_sha256"],c.certificate_pin)||!parse_hex(values["credential"],c.credential)||!parse_hex(values["build_sha256"],c.identity.build)||!parse_hex(values["executable_sha256"],c.identity.executable)||!parse_hex(values["content_sha256"],c.identity.content)||!parse_hex(values["fixture_sha256"],c.identity.fixture)){error="invalid_digest_or_credential";return false;}
    Digest zero{};if(c.certificate_pin==zero||c.credential==zero||c.identity.build==zero||c.identity.executable==zero||c.identity.content==zero||c.identity.fixture==zero){error="empty_identity_or_credential";return false;}
    for(size_t index=0;index<world_file_count;++index) {
        const auto field=world_files[index].field;
        if(!values.count(field)||!parse_hex(values[field],c.identity.world[index])||c.identity.world[index]==Digest{}) {
            error=std::string("missing_or_invalid_world_identity:")+world_files[index].path;return false;
        }
    }
    if(c.identity.fixture!=c.identity.world[0]){error="world_fixture_identity_disagrees";return false;}
    result=c;error.clear();return true;
}
}
