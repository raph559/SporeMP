#include "channel.h"
#include <chrono>

namespace sporemp::network::detail {
Channel::~Channel(){stop();}
void Channel::start_client(const PeerConfig& c){client_=true;Packet hello;hello.kind=Kind::hello;hello.role=c.role;hello.identity=c.identity;hello.credential=c.credential;send(hello);thread_=std::thread([this,c]{run(INVALID_SOCKET,nullptr,c);});}
void Channel::start_server(SOCKET socket,TlsIdentity* identity){thread_=std::thread([this,socket,identity]{run(socket,identity,{});});}
bool Channel::send(Packet p){std::lock_guard<std::mutex> lock(mutex_);if(stop_||finished_||outgoing_.size()>=queue_capacity){stop_=true;return false;}if(client_){p.sequence=++sequence_;p.session=session_;}outgoing_.push_back(std::move(p));return true;}
bool Channel::push(Event e){std::lock_guard<std::mutex> lock(mutex_);if(incoming_.size()>=queue_capacity){stop_=true;return false;}incoming_.push_back(std::move(e));return true;}
bool Channel::poll(Event& e){std::lock_guard<std::mutex> lock(mutex_);if(incoming_.empty())return false;e=std::move(incoming_.front());incoming_.pop_front();return true;}
void Channel::stop(){stop_=true;if(thread_.joinable())thread_.join();}
void Channel::run(SOCKET socket,TlsIdentity* identity,PeerConfig config){
    std::string error;
    if(client_)socket=connect_socket(config.host,config.port,stop_,error);
    if(socket==INVALID_SOCKET){push({EventKind::disconnected,{},error});finished_=true;return;}
    TlsStream stream(socket);
    if(!stream.handshake(!client_,identity,config.certificate_pin,config.host,stop_,error)){push({EventKind::disconnected,{},error});finished_=true;return;}
    push({EventKind::connected,{},"tls12_authenticated_server"});
    std::vector<uint8_t> plain;plain.reserve(32768);auto last_received=std::chrono::steady_clock::now(),last_ping=last_received,rate_start=last_received;size_t rate_count=0;uint64_t last_sequence=0;
    while(!stop_){
        std::vector<uint8_t> batch;batch.reserve(packet_bytes*32);
        {std::lock_guard<std::mutex> lock(mutex_);for(size_t n=0;n<32&&!outgoing_.empty();++n){Wire wire=encode(outgoing_.front());outgoing_.pop_front();batch.insert(batch.end(),wire.begin(),wire.end());}}
        if(!batch.empty()&&!stream.send(batch.data(),batch.size(),error))break;
        if(!stream.receive(plain,error))break;
        auto now=std::chrono::steady_clock::now();if(now-rate_start>std::chrono::seconds(1)){rate_start=now;rate_count=0;}
        size_t used=0;
        while(plain.size()-used>=packet_bytes){Packet packet;if(!decode(plain.data()+used,packet_bytes,packet,error)){stop_=true;break;}used+=packet_bytes;
            if(++rate_count>max_entities*25+256){error="packet_rate_exceeded";stop_=true;break;}
            if(!packet.sequence||packet.sequence<=last_sequence){error="replayed_stream_sequence";stop_=true;break;}last_sequence=packet.sequence;
            if(client_&&packet.kind==Kind::welcome)session_=packet.session;
            if(client_&&session_&&packet.session!=session_){error="wrong_session_epoch";stop_=true;break;}
            last_received=now;if(packet.kind!=Kind::ping&&!push({EventKind::packet,packet,{}})){error="incoming_queue_full";break;}
            // Server application handles ping so it can allocate the stream sequence.
            if(!client_&&packet.kind==Kind::ping&&!push({EventKind::packet,packet,{}})){error="incoming_queue_full";break;}
        }
        if(used)plain.erase(plain.begin(),plain.begin()+static_cast<ptrdiff_t>(used));
        if(client_&&session_&&now-last_ping>std::chrono::seconds(2)){Packet ping;ping.kind=Kind::ping;if(!send(ping)){error="outgoing_queue_full";break;}last_ping=now;}
        if(now-last_received>std::chrono::seconds(10)){error="peer_timeout";break;}
    }
    if(error.empty())error=stop_?"stopped_or_queue_full":"disconnected";
    // Ensure terminal state remains observable even after a queue overflow.
    {std::lock_guard<std::mutex> lock(mutex_);if(incoming_.size()>=queue_capacity)incoming_.pop_front();incoming_.push_back({EventKind::disconnected,{},error});}
    finished_=true;
}
}
