#include "coordinator.h"
#include "channel.h"
#include <map>

namespace sporemp::network {
struct Coordinator::Impl {
    SOCKET listener=INVALID_SOCKET;CoordinatorConfig config{};detail::TlsIdentity identity;std::unique_ptr<Session> session;
    std::map<uint64_t,std::unique_ptr<detail::Channel>> channels;std::deque<CoordinatorEvent> events;uint64_t next_connection=0;
    void dispatch(const std::vector<Dispatch>& messages){for(const auto& d:messages){auto c=channels.find(d.connection);if(c==channels.end())continue;c->second->send(d.packet);if(d.close){/* Rejection is delivered before close; application deletes after a short flush below. */closing[d.connection]=GetTickCount64()+100;}}}
    std::map<uint64_t,uint64_t> closing;
};
Coordinator::Coordinator():impl_(std::make_unique<Impl>()){}
Coordinator::~Coordinator(){stop();}
bool Coordinator::start(CoordinatorConfig config,std::string& e){auto& x=*impl_;if(x.session){e="coordinator_already_started";return false;}if(!detail::initialize_sockets(e)||!x.identity.create(e))return false;Digest zero{};for(auto secret:{&config.session.authority,&config.session.player1,&config.session.player2})if(*secret==zero&&!detail::random_bytes(secret->data(),secret->size())){e="credential_random_failed";return false;}if(config.session.authority==config.session.player1||config.session.authority==config.session.player2||config.session.player1==config.session.player2){e="credentials_must_be_distinct";return false;}if(!detail::random_bytes(&config.session.epoch,sizeof(config.session.epoch))||!config.session.epoch){e="session_random_failed";return false;}x.listener=detail::listen_socket(config.bind_host,config.port,config.port,e);if(x.listener==INVALID_SOCKET)return false;x.config=config;x.session=std::make_unique<Session>(config.session);return true;}
PeerConfig Coordinator::peer_config(Role role,uint64_t player)const{const auto& x=*impl_;PeerConfig c;c.host=x.config.bind_host;c.port=x.config.port;c.certificate_pin=x.identity.pin;c.identity=x.config.session.identity;c.role=role;c.credential=role==Role::authority?x.config.session.authority:player==1?x.config.session.player1:x.config.session.player2;return c;}
bool Coordinator::poll(CoordinatorEvent& event){auto& x=*impl_;if(!x.session)return false;
    for(size_t n=0;n<8;++n){SOCKET socket=accept(x.listener,nullptr,nullptr);if(socket==INVALID_SOCKET)break;if(x.channels.size()>=8){closesocket(socket);continue;}u_long blocking=0;ioctlsocket(socket,FIONBIO,&blocking);uint64_t id=++x.next_connection;if(!x.session->connect(id)){closesocket(socket);continue;}auto channel=std::make_unique<detail::Channel>();channel->start_server(socket,&x.identity);x.channels.emplace(id,std::move(channel));}
    std::vector<uint64_t> remove;
    for(auto& entry:x.channels){Event incoming;size_t n=0;while(n++<4096&&entry.second->poll(incoming)){if(incoming.kind==EventKind::packet&&!x.closing.count(entry.first))x.dispatch(x.session->receive(entry.first,incoming.packet));if(incoming.kind==EventKind::disconnected){x.dispatch(x.session->disconnect(entry.first));remove.push_back(entry.first);}if(x.events.size()<queue_capacity)x.events.push_back({entry.first,std::move(incoming)});else {entry.second->stop();x.dispatch(x.session->disconnect(entry.first));remove.push_back(entry.first);break;}}auto close=x.closing.find(entry.first);if(close!=x.closing.end()&&GetTickCount64()>=close->second){x.dispatch(x.session->disconnect(entry.first));remove.push_back(entry.first);}}
    for(auto id:remove){x.channels.erase(id);x.closing.erase(id);}
    if(x.events.empty())return false;event=std::move(x.events.front());x.events.pop_front();return true;
}
void Coordinator::stop(){auto& x=*impl_;if(x.listener!=INVALID_SOCKET){closesocket(x.listener);x.listener=INVALID_SOCKET;}x.channels.clear();x.session.reset();}
}
