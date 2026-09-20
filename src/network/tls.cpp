#include "tls.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <mutex>
#include <sstream>

namespace sporemp::network::detail {
namespace {
std::string status_error(const char* step,long code){std::ostringstream s;s<<step<<":0x"<<std::hex<<static_cast<unsigned long>(code);return s.str();}
void socket_options(SOCKET s){DWORD timeout=1000;setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));timeout=100;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));BOOL yes=TRUE;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof(yes));}
bool readable(SOCKET s,long microseconds){fd_set f;FD_ZERO(&f);FD_SET(s,&f);timeval t{0,microseconds};return select(0,&f,nullptr,nullptr,&t)>0;}
}
bool initialize_sockets(std::string& e){static std::once_flag once;static int result=0;std::call_once(once,[]{WSADATA w{};result=WSAStartup(MAKEWORD(2,2),&w);});if(result){e=status_error("WSAStartup",result);return false;}return true;}
bool random_bytes(void* p,size_t n){return n<=0xffffffffULL&&BCryptGenRandom(nullptr,static_cast<PUCHAR>(p),static_cast<ULONG>(n),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;}
TlsIdentity::~TlsIdentity(){if(acquired)FreeCredentialsHandle(&credentials);if(certificate)CertFreeCertificateContext(certificate);if(key)NCryptDeleteKey(key,0);if(provider)NCryptFreeObject(provider);}
bool TlsIdentity::create(std::string& e){
    Digest random{};if(!random_bytes(random.data(),random.size())){e="random_failed";return false;}auto suffix=hex(random);key_name=L"SporeMP-M06-"+std::wstring(suffix.begin(),suffix.end());
    auto status=NCryptOpenStorageProvider(&provider,MS_KEY_STORAGE_PROVIDER,0);if(status){e=status_error("NCryptOpenStorageProvider",status);return false;}
    status=NCryptCreatePersistedKey(provider,&key,NCRYPT_RSA_ALGORITHM,key_name.c_str(),0,0);if(status){e=status_error("NCryptCreatePersistedKey",status);return false;}
    DWORD length=2048;status=NCryptSetProperty(key,NCRYPT_LENGTH_PROPERTY,reinterpret_cast<PBYTE>(&length),sizeof(length),0);if(status||NCryptFinalizeKey(key,0)){e="certificate_key_generation_failed";return false;}
    DWORD subject_size=0;CertStrToNameW(X509_ASN_ENCODING,L"CN=SporeMP private M06 session",CERT_X500_NAME_STR,nullptr,nullptr,&subject_size,nullptr);std::vector<BYTE> subject(subject_size);if(!subject_size||!CertStrToNameW(X509_ASN_ENCODING,L"CN=SporeMP private M06 session",CERT_X500_NAME_STR,nullptr,subject.data(),&subject_size,nullptr)){e="certificate_subject_failed";return false;}
    CERT_NAME_BLOB name{subject_size,subject.data()};CRYPT_KEY_PROV_INFO info{};info.pwszContainerName=key_name.data();info.pwszProvName=const_cast<LPWSTR>(MS_KEY_STORAGE_PROVIDER);info.dwKeySpec=0; // CNG NCryptOpenKey legacy key specification, not CERT_KEY_CONTEXT key type.
    CRYPT_ALGORITHM_IDENTIFIER algorithm{};algorithm.pszObjId=const_cast<LPSTR>(szOID_RSA_SHA256RSA);
    certificate=CertCreateSelfSignCertificate(key,&name,0,&info,&algorithm,nullptr,nullptr,nullptr);if(!certificate){e=status_error("CertCreateSelfSignCertificate",GetLastError());return false;}
    DWORD size=static_cast<DWORD>(pin.size());if(!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,nullptr,certificate->pbCertEncoded,certificate->cbCertEncoded,pin.data(),&size)){e="certificate_hash_failed";return false;}
    SCHANNEL_CRED cred{};cred.dwVersion=SCHANNEL_CRED_VERSION;cred.cCreds=1;cred.paCred=&certificate;cred.grbitEnabledProtocols=SP_PROT_TLS1_2_SERVER;cred.dwFlags=SCH_USE_STRONG_CRYPTO|SCH_CRED_NO_SYSTEM_MAPPER;
    TimeStamp expires{};status=AcquireCredentialsHandleW(nullptr,const_cast<LPWSTR>(UNISP_NAME_W),SECPKG_CRED_INBOUND,nullptr,&cred,nullptr,nullptr,&credentials,&expires);if(status){e=status_error("AcquireCredentialsHandle(server)",status);return false;}acquired=true;return true;
}
TlsStream::~TlsStream(){if(context_valid_)DeleteSecurityContext(&context_);if(own_credentials_)FreeCredentialsHandle(&credentials_);if(socket_!=INVALID_SOCKET){shutdown(socket_,SD_BOTH);closesocket(socket_);}}
bool TlsStream::raw_send(const uint8_t* p,size_t n,std::string& e){while(n){int sent=::send(socket_,reinterpret_cast<const char*>(p),static_cast<int>(n),0);if(sent<=0){e=status_error("socket_send",WSAGetLastError());return false;}p+=sent;n-=static_cast<size_t>(sent);}return true;}
bool TlsStream::read_more(std::string& e){uint8_t bytes[16384];int n=recv(socket_,reinterpret_cast<char*>(bytes),sizeof(bytes),0);if(n<=0){e=n==0?"peer_closed":status_error("socket_receive",WSAGetLastError());return false;}if(encrypted_.size()+static_cast<size_t>(n)>65536){e="tls_receive_capacity";return false;}encrypted_.insert(encrypted_.end(),bytes,bytes+n);return true;}
bool TlsStream::handshake(bool server,TlsIdentity* identity,const Digest& pin,const std::string& host,const std::atomic<bool>& stop,std::string& e){
    socket_options(socket_);CredHandle* credential=nullptr;
    if(server)credential=&identity->credentials;
    else {SCHANNEL_CRED cred{};cred.dwVersion=SCHANNEL_CRED_VERSION;cred.grbitEnabledProtocols=SP_PROT_TLS1_2_CLIENT;cred.dwFlags=SCH_CRED_MANUAL_CRED_VALIDATION|SCH_CRED_NO_DEFAULT_CREDS|SCH_USE_STRONG_CRYPTO;TimeStamp expires{};auto s=AcquireCredentialsHandleW(nullptr,const_cast<LPWSTR>(UNISP_NAME_W),SECPKG_CRED_OUTBOUND,nullptr,&cred,nullptr,nullptr,&credentials_,&expires);if(s){e=status_error("AcquireCredentialsHandle(client)",s);return false;}own_credentials_=true;credential=&credentials_;}
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);bool first=true,need_read=server;std::wstring target(host.begin(),host.end());
    while(!stop&&std::chrono::steady_clock::now()<deadline){
        if(need_read){if(!readable(socket_,10000))continue;if(!read_more(e))return false;}
        SecBuffer in[2]{{static_cast<unsigned long>(encrypted_.size()),SECBUFFER_TOKEN,encrypted_.data()},{0,SECBUFFER_EMPTY,nullptr}};SecBufferDesc input{SECBUFFER_VERSION,2,in};
        SecBuffer output_buffer{0,SECBUFFER_TOKEN,nullptr};SecBufferDesc output{SECBUFFER_VERSION,1,&output_buffer};ULONG attributes=0;TimeStamp expires{};
        const ULONG flags=server?(ASC_REQ_SEQUENCE_DETECT|ASC_REQ_REPLAY_DETECT|ASC_REQ_CONFIDENTIALITY|ASC_REQ_EXTENDED_ERROR|ASC_REQ_ALLOCATE_MEMORY|ASC_REQ_STREAM):(ISC_REQ_SEQUENCE_DETECT|ISC_REQ_REPLAY_DETECT|ISC_REQ_CONFIDENTIALITY|ISC_REQ_EXTENDED_ERROR|ISC_REQ_ALLOCATE_MEMORY|ISC_REQ_STREAM|ISC_REQ_MANUAL_CRED_VALIDATION);
        SECURITY_STATUS s=server?AcceptSecurityContext(credential,first?nullptr:&context_,&input,flags,SECURITY_NATIVE_DREP,&context_,&output,&attributes,&expires):InitializeSecurityContextW(credential,first?nullptr:&context_,target.data(),flags,0,SECURITY_NATIVE_DREP,first?nullptr:&input,0,&context_,&output,&attributes,&expires);
        if(s==SEC_E_OK||s==SEC_I_CONTINUE_NEEDED){context_valid_=true;first=false;}
        if(output_buffer.pvBuffer){bool sent=raw_send(static_cast<uint8_t*>(output_buffer.pvBuffer),output_buffer.cbBuffer,e);FreeContextBuffer(output_buffer.pvBuffer);if(!sent)return false;}
        if(s==SEC_E_INCOMPLETE_MESSAGE){need_read=true;continue;}
        if(s!=SEC_E_OK&&s!=SEC_I_CONTINUE_NEEDED){e=status_error("tls_handshake",s);return false;}
        size_t extra=in[1].BufferType==SECBUFFER_EXTRA?in[1].cbBuffer:0;
        if(extra&&extra<=encrypted_.size())encrypted_.erase(encrypted_.begin(),encrypted_.end()-static_cast<ptrdiff_t>(extra));else encrypted_.clear();
        if(s==SEC_E_OK){
            if(!server){PCCERT_CONTEXT remote=nullptr;s=QueryContextAttributesW(&context_,SECPKG_ATTR_REMOTE_CERT_CONTEXT,&remote);if(s||!remote){e="missing_server_certificate";return false;}Digest actual{};DWORD size=static_cast<DWORD>(actual.size());bool hash=CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,nullptr,remote->pbCertEncoded,remote->cbCertEncoded,actual.data(),&size)!=FALSE;bool time_valid=CertVerifyTimeValidity(nullptr,remote->pCertInfo)==0;CertFreeCertificateContext(remote);uint32_t diff=0;for(size_t i=0;i<pin.size();++i)diff|=pin[i]^actual[i];if(!hash||!time_valid||diff){e="certificate_pin_or_validity_mismatch";return false;}}
            s=QueryContextAttributesW(&context_,SECPKG_ATTR_STREAM_SIZES,&sizes_);if(s){e=status_error("tls_stream_sizes",s);return false;}
            SecPkgContext_ConnectionInfo connection{};s=QueryContextAttributesW(&context_,SECPKG_ATTR_CONNECTION_INFO,&connection);if(s||connection.dwCipherStrength<128){e="tls_cipher_not_accepted";return false;}return true;
        }
        need_read=encrypted_.empty();
    }
    e=stop?"stopped":"tls_handshake_timeout";return false;
}
bool TlsStream::send(const uint8_t* data,size_t n,std::string& e){
    while(n){size_t take=(std::min)(n,static_cast<size_t>(sizes_.cbMaximumMessage));std::vector<uint8_t> block(sizes_.cbHeader+take+sizes_.cbTrailer);std::memcpy(block.data()+sizes_.cbHeader,data,take);SecBuffer b[4]{{sizes_.cbHeader,SECBUFFER_STREAM_HEADER,block.data()},{static_cast<ULONG>(take),SECBUFFER_DATA,block.data()+sizes_.cbHeader},{sizes_.cbTrailer,SECBUFFER_STREAM_TRAILER,block.data()+sizes_.cbHeader+take},{0,SECBUFFER_EMPTY,nullptr}};SecBufferDesc desc{SECBUFFER_VERSION,4,b};auto s=EncryptMessage(&context_,0,&desc,0);if(s){e=status_error("EncryptMessage",s);return false;}if(!raw_send(block.data(),size_t(b[0].cbBuffer)+b[1].cbBuffer+b[2].cbBuffer,e))return false;data+=take;n-=take;}return true;
}
bool TlsStream::receive(std::vector<uint8_t>& plaintext,std::string& e){
    if(encrypted_.empty()){if(!readable(socket_,1000))return true;if(!read_more(e))return false;}
    for(;;){SecBuffer b[4]{{static_cast<ULONG>(encrypted_.size()),SECBUFFER_DATA,encrypted_.data()},{0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr}};SecBufferDesc desc{SECBUFFER_VERSION,4,b};auto s=DecryptMessage(&context_,&desc,0,nullptr);if(s==SEC_E_INCOMPLETE_MESSAGE){if(readable(socket_,0))return read_more(e);return true;}if(s!=SEC_E_OK){e=status_error("DecryptMessage",s);return false;}size_t extra=0;for(const auto& part:b){if(part.BufferType==SECBUFFER_DATA){if(plaintext.size()+part.cbBuffer>65536){e="plaintext_capacity";return false;}auto p=static_cast<uint8_t*>(part.pvBuffer);plaintext.insert(plaintext.end(),p,p+part.cbBuffer);}else if(part.BufferType==SECBUFFER_EXTRA)extra=part.cbBuffer;}if(extra&&extra<=encrypted_.size())encrypted_.erase(encrypted_.begin(),encrypted_.end()-static_cast<ptrdiff_t>(extra));else encrypted_.clear();if(encrypted_.empty())return true;}
}
SOCKET connect_socket(const std::string& host,uint16_t port,const std::atomic<bool>& stop,std::string& e){
    if(!initialize_sockets(e))return INVALID_SOCKET;addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;addrinfo* addresses=nullptr;auto service=std::to_string(port);int rc=getaddrinfo(host.c_str(),service.c_str(),&hints,&addresses);if(rc){e=status_error("getaddrinfo",rc);return INVALID_SOCKET;}SOCKET connected=INVALID_SOCKET;
    for(auto a=addresses;a&&!stop;a=a->ai_next){SOCKET s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(s==INVALID_SOCKET)continue;u_long nonblocking=1;ioctlsocket(s,FIONBIO,&nonblocking);rc=connect(s,a->ai_addr,static_cast<int>(a->ai_addrlen));if(rc==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!stop&&std::chrono::steady_clock::now()<deadline){fd_set wr,ex;FD_ZERO(&wr);FD_ZERO(&ex);FD_SET(s,&wr);FD_SET(s,&ex);timeval t{0,10000};if(select(0,nullptr,&wr,&ex,&t)>0){int error=0;int size=sizeof(error);getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size);rc=error?SOCKET_ERROR:0;break;}}}if(rc==0){nonblocking=0;ioctlsocket(s,FIONBIO,&nonblocking);connected=s;break;}closesocket(s);}
    freeaddrinfo(addresses);if(connected==INVALID_SOCKET)e="tcp_connect_failed_or_timeout";return connected;
}
SOCKET listen_socket(const std::string& host,uint16_t port,uint16_t& actual,std::string& e){if(!initialize_sockets(e))return INVALID_SOCKET;SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET){e="socket_create_failed";return s;}BOOL yes=TRUE;setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&yes),sizeof(yes));sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,host.c_str(),&a.sin_addr)!=1||bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))==SOCKET_ERROR||listen(s,8)==SOCKET_ERROR){e=status_error("listen_or_bind",WSAGetLastError());closesocket(s);return INVALID_SOCKET;}int n=sizeof(a);getsockname(s,reinterpret_cast<sockaddr*>(&a),&n);actual=ntohs(a.sin_port);u_long nonblocking=1;ioctlsocket(s,FIONBIO,&nonblocking);return s;}
}
