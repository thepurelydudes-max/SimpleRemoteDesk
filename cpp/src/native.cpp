#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <iphlpapi.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"crypt32.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"iphlpapi.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"dwmapi.lib")

using byte = std::uint8_t;
using Clock = std::chrono::steady_clock;

static constexpr std::uint32_t HOST_PREFIX=0x48535433u;
static constexpr std::uint32_t VIEWER_PREFIX=0x56575233u;
namespace pkt {
constexpr byte Screen=1, MouseMove=2, MouseButton=3, MouseWheel=4, Key=5,
 AudioFormat=6, Audio=7, KeyCombination=8, Preview=9, Cursor=10,
 FileClipboardGet=20, FileManifest=21, FileChunk=22, FileTransferEnd=23,
 FileClipboardPut=24, FileTransferAck=25;
}

static COLORREF C_BG=RGB(10,20,36), C_HEADER=RGB(13,27,46), C_SURFACE=RGB(17,34,58),
 C_SURFACE2=RGB(23,43,71), C_SURFACE3=RGB(31,57,92), C_BORDER=RGB(66,106,151),
 C_BORDER_SOFT=RGB(49,79,115), C_ACCENT=RGB(30,132,255), C_TEXT=RGB(245,248,253),
 C_MUTED=RGB(156,177,207), C_MUTED2=RGB(118,143,177), C_SUCCESS=RGB(51,211,153),
 C_OFFLINE=RGB(126,145,174), C_DANGER=RGB(246,87,100);

struct Wsa {
    Wsa(){ WSADATA w{}; if(WSAStartup(MAKEWORD(2,2),&w)!=0) throw std::runtime_error("WSAStartup"); }
    ~Wsa(){ WSACleanup(); }
};
struct GdiPlus {
    ULONG_PTR token{};
    GdiPlus(){ Gdiplus::GdiplusStartupInput in; if(Gdiplus::GdiplusStartup(&token,&in,nullptr)!=Gdiplus::Ok) throw std::runtime_error("GDI+"); }
    ~GdiPlus(){ if(token) Gdiplus::GdiplusShutdown(token); }
};
struct Sock {
    SOCKET s=INVALID_SOCKET;
    Sock()=default; explicit Sock(SOCKET x):s(x){}
    Sock(const Sock&)=delete; Sock& operator=(const Sock&)=delete;
    Sock(Sock&&o) noexcept:s(std::exchange(o.s,INVALID_SOCKET)){}
    Sock& operator=(Sock&&o) noexcept { if(this!=&o){close();s=std::exchange(o.s,INVALID_SOCKET);} return *this; }
    ~Sock(){close();}
    void close(){ if(s!=INVALID_SOCKET){ shutdown(s,SD_BOTH); closesocket(s); s=INVALID_SOCKET; } }
    explicit operator bool() const{return s!=INVALID_SOCKET;}
};

static std::wstring u8w(std::string_view s){
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
    std::wstring w(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n); return w;
}
static std::string wu8(std::wstring_view w){
    if(w.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0'); WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr); return s;
}
static void ensure_dir(const std::filesystem::path&p){ std::error_code ec; std::filesystem::create_directories(p,ec); }
static std::filesystem::path app_dir(){
    PWSTR p=nullptr; std::filesystem::path d;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&p))){d=p;CoTaskMemFree(p);}
    d/=L"SimpleRemoteDesk"; ensure_dir(d); return d;
}
static void log_line(std::wstring_view m){
    try{ auto p=app_dir()/L"native.log"; std::wofstream f(p,std::ios::app); SYSTEMTIME t{};GetLocalTime(&t);
      f<<std::setfill(L'0')<<std::setw(4)<<t.wYear<<L"-"<<std::setw(2)<<t.wMonth<<L"-"<<std::setw(2)<<t.wDay<<L" "
       <<std::setw(2)<<t.wHour<<L":"<<std::setw(2)<<t.wMinute<<L":"<<std::setw(2)<<t.wSecond<<L" "<<m<<L"\n"; }catch(...){}
}
static void tcp_opts(SOCKET s){
    BOOL one=TRUE; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(char*)&one,sizeof(one));
    int bs=2*1024*1024; setsockopt(s,SOL_SOCKET,SO_RCVBUF,(char*)&bs,sizeof(bs)); setsockopt(s,SOL_SOCKET,SO_SNDBUF,(char*)&bs,sizeof(bs));
}
static bool send_all(SOCKET s,std::span<const byte>d){size_t o=0;while(o<d.size()){int n=send(s,(char*)d.data()+o,(int)std::min<size_t>(d.size()-o,1<<20),0);if(n<=0)return false;o+=n;}return true;}
static bool recv_all(SOCKET s,std::span<byte>d){size_t o=0;while(o<d.size()){int n=recv(s,(char*)d.data()+o,(int)std::min<size_t>(d.size()-o,1<<20),0);if(n<=0)return false;o+=n;}return true;}
static void le32(byte*p,std::int32_t v){memcpy(p,&v,4);} static void le64(byte*p,std::int64_t v){memcpy(p,&v,8);}
static std::int32_t rd32(const byte*p){std::int32_t v;memcpy(&v,p,4);return v;} static std::int64_t rd64(const byte*p){std::int64_t v;memcpy(&v,p,8);return v;}
static Sock listen_tcp(int port){
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); if(s==INVALID_SOCKET)return{};
    BOOL one=TRUE;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char*)&one,sizeof(one));
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=INADDR_ANY;a.sin_port=htons((u_short)port);
    if(bind(s,(sockaddr*)&a,sizeof(a))||listen(s,16)){closesocket(s);return{};} return Sock(s);
}
static Sock connect_tcp(std::string host,int port,int timeout=6000){
    addrinfo h{},*r=nullptr;h.ai_family=AF_UNSPEC;h.ai_socktype=SOCK_STREAM;h.ai_protocol=IPPROTO_TCP;
    std::string ps=std::to_string(port); if(getaddrinfo(host.c_str(),ps.c_str(),&h,&r))return{};
    Sock out;
    for(auto*p=r;p&&!out;p=p->ai_next){ SOCKET s=socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(s==INVALID_SOCKET)continue;
      u_long nb=1;ioctlsocket(s,FIONBIO,&nb);int rc=connect(s,p->ai_addr,(int)p->ai_addrlen);
      if(rc==0){nb=0;ioctlsocket(s,FIONBIO,&nb);tcp_opts(s);out=Sock(s);break;}
      if(WSAGetLastError()==WSAEWOULDBLOCK||WSAGetLastError()==WSAEINPROGRESS){fd_set w;FD_ZERO(&w);FD_SET(s,&w);timeval tv{timeout/1000,(timeout%1000)*1000};if(select(0,nullptr,&w,nullptr,&tv)>0){int er=0,l=sizeof(er);getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&er,&l);if(!er){nb=0;ioctlsocket(s,FIONBIO,&nb);tcp_opts(s);out=Sock(s);break;}}}
      closesocket(s);
    } freeaddrinfo(r); return out;
}
static std::string peer_ip(SOCKET s){sockaddr_storage a{};int n=sizeof(a);char b[INET6_ADDRSTRLEN]{};if(getpeername(s,(sockaddr*)&a,&n))return{};if(a.ss_family==AF_INET)inet_ntop(AF_INET,&((sockaddr_in*)&a)->sin_addr,b,sizeof b);else inet_ntop(AF_INET6,&((sockaddr_in6*)&a)->sin6_addr,b,sizeof b);return b;}

static void ntok(NTSTATUS x,const char*msg){if(x<0)throw std::runtime_error(msg);}
static std::array<byte,32> pbkdf2(std::string_view pass,std::span<const byte>salt){
    BCRYPT_ALG_HANDLE a{};ntok(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG),"PBKDF2 alg");
    std::array<byte,32>o{};auto st=BCryptDeriveKeyPBKDF2(a,(PUCHAR)pass.data(),(ULONG)pass.size(),(PUCHAR)salt.data(),(ULONG)salt.size(),120000,o.data(),(ULONG)o.size(),0);BCryptCloseAlgorithmProvider(a,0);ntok(st,"PBKDF2");return o;
}
static std::array<byte,32> hmac(std::span<const byte>key,std::span<const byte>data){
    BCRYPT_ALG_HANDLE a{};BCRYPT_HASH_HANDLE h{};DWORD obj=0,cb=0;ntok(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG),"HMAC alg");
    ntok(BCryptGetProperty(a,BCRYPT_OBJECT_LENGTH,(PUCHAR)&obj,sizeof(obj),&cb,0),"HMAC prop");std::vector<byte>ob(obj);std::array<byte,32>o{};
    ntok(BCryptCreateHash(a,&h,ob.data(),obj,(PUCHAR)key.data(),(ULONG)key.size(),0),"HMAC create");ntok(BCryptHashData(h,(PUCHAR)data.data(),(ULONG)data.size(),0),"HMAC data");ntok(BCryptFinishHash(h,o.data(),32,0),"HMAC finish");BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);return o;
}
static bool eq(std::span<const byte>a,std::span<const byte>b){if(a.size()!=b.size())return false;byte x=0;for(size_t i=0;i<a.size();++i)x|=a[i]^b[i];return x==0;}
static std::array<byte,32> auth_server(SOCKET s,std::string_view pass){
    std::array<byte,4>magic{'S','R','D','3'};std::array<byte,16>salt{};std::array<byte,32>challenge{};
    BCryptGenRandom(nullptr,salt.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG);BCryptGenRandom(nullptr,challenge.data(),32,BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if(!send_all(s,magic)||!send_all(s,salt)||!send_all(s,challenge))throw std::runtime_error("auth send");
    auto key=pbkdf2(pass,salt);auto ex=hmac(key,challenge);std::array<byte,32>got{};if(!recv_all(s,got))throw std::runtime_error("auth recv");byte ok=eq(ex,got)?1:0;send_all(s,std::span<const byte>(&ok,1));if(!ok)throw std::runtime_error("bad password");return key;
}
static std::array<byte,32> auth_client(SOCKET s,std::string_view pass){
    std::array<byte,4>magic{};std::array<byte,16>salt{};std::array<byte,32>challenge{};if(!recv_all(s,magic)||memcmp(magic.data(),"SRD3",4)||!recv_all(s,salt)||!recv_all(s,challenge))throw std::runtime_error("auth");
    auto key=pbkdf2(pass,salt);auto v=hmac(key,challenge);if(!send_all(s,v))throw std::runtime_error("auth send");byte ok{};if(!recv_all(s,std::span<byte>(&ok,1))||ok!=1)throw std::runtime_error("bad password");return key;
}

struct Packet{byte type{};std::vector<byte>payload;};
class Channel{
    SOCKET s_;BCRYPT_ALG_HANDLE alg_{};BCRYPT_KEY_HANDLE key_{};std::vector<byte>obj_;std::uint32_t sp_,rp_;std::atomic<std::int64_t>seq_{0};std::mutex sm_;
public:
    Channel(SOCKET s,std::span<const byte>k,std::uint32_t sp,std::uint32_t rp):s_(s),sp_(sp),rp_(rp){
      ntok(BCryptOpenAlgorithmProvider(&alg_,BCRYPT_AES_ALGORITHM,nullptr,0),"AES");ntok(BCryptSetProperty(alg_,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_GCM,sizeof(BCRYPT_CHAIN_MODE_GCM),0),"GCM");
      DWORD n=0,cb=0;ntok(BCryptGetProperty(alg_,BCRYPT_OBJECT_LENGTH,(PUCHAR)&n,sizeof(n),&cb,0),"AES obj");obj_.resize(n);ntok(BCryptGenerateSymmetricKey(alg_,&key_,obj_.data(),n,(PUCHAR)k.data(),(ULONG)k.size(),0),"AES key");
    }
    ~Channel(){if(key_)BCryptDestroyKey(key_);if(alg_)BCryptCloseAlgorithmProvider(alg_,0);}
    bool send(byte type,std::span<const byte>p={}){
      std::lock_guard lk(sm_);std::vector<byte>plain(1+p.size());plain[0]=type;if(!p.empty())memcpy(plain.data()+1,p.data(),p.size());
      std::vector<byte>ct(plain.size());std::array<byte,16>tag{};std::array<byte,12>nonce{};auto q=++seq_;memcpy(nonce.data(),&sp_,4);le64(nonce.data()+4,q);std::array<byte,8>aad{};le64(aad.data(),q);
      BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai;BCRYPT_INIT_AUTH_MODE_INFO(ai);ai.pbNonce=nonce.data();ai.cbNonce=12;ai.pbAuthData=aad.data();ai.cbAuthData=8;ai.pbTag=tag.data();ai.cbTag=16;ULONG done=0;
      if(BCryptEncrypt(key_,plain.data(),(ULONG)plain.size(),&ai,nullptr,0,ct.data(),(ULONG)ct.size(),&done,0)<0)return false;
      std::array<byte,28>h{};le32(h.data(),(int)ct.size());le64(h.data()+4,q);memcpy(h.data()+12,tag.data(),16);return send_all(s_,h)&&send_all(s_,ct);
    }
    bool send2(byte type,std::span<const byte>a,std::span<const byte>b){std::vector<byte>p;p.reserve(a.size()+b.size());p.insert(p.end(),a.begin(),a.end());p.insert(p.end(),b.begin(),b.end());return send(type,p);}
    std::optional<Packet> recv(){
      std::array<byte,28>h{};if(!recv_all(s_,h))return{};int n=rd32(h.data());auto q=rd64(h.data()+4);if(n<=0||n>64*1024*1024)return{};std::vector<byte>ct(n),pt(n);if(!recv_all(s_,ct))return{};
      std::array<byte,12>nonce{};memcpy(nonce.data(),&rp_,4);le64(nonce.data()+4,q);std::array<byte,8>aad{};le64(aad.data(),q);
      BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai;BCRYPT_INIT_AUTH_MODE_INFO(ai);ai.pbNonce=nonce.data();ai.cbNonce=12;ai.pbAuthData=aad.data();ai.cbAuthData=8;ai.pbTag=h.data()+12;ai.cbTag=16;ULONG done=0;
      if(BCryptDecrypt(key_,ct.data(),n,&ai,nullptr,0,pt.data(),n,&done,0)<0||done<1)return{};Packet p;p.type=pt[0];p.payload.assign(pt.begin()+1,pt.begin()+done);return p;
    }
};

static int jpeg_clsid(CLSID&out){UINT n=0,z=0;Gdiplus::GetImageEncodersSize(&n,&z);if(!z)return 0;std::vector<byte>b(z);auto*p=(Gdiplus::ImageCodecInfo*)b.data();Gdiplus::GetImageEncoders(n,z,p);for(UINT i=0;i<n;i++)if(wcscmp(p[i].MimeType,L"image/jpeg")==0){out=p[i].Clsid;return 1;}return 0;}
class Capture{
    HDC screen_{},mem_{};HBITMAP bmp_{};HGDIOBJ old_{};int w_{},h_{};CLSID jpg_{};ULONG quality_;
public:
    explicit Capture(ULONG q=95):quality_(q){w_=GetSystemMetrics(SM_CXSCREEN);h_=GetSystemMetrics(SM_CYSCREEN);screen_=GetDC(nullptr);mem_=CreateCompatibleDC(screen_);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w_;bi.bmiHeader.biHeight=-h_;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;void*bits=nullptr;bmp_=CreateDIBSection(screen_,&bi,DIB_RGB_COLORS,&bits,nullptr,0);old_=SelectObject(mem_,bmp_);jpeg_clsid(jpg_);}
    ~Capture(){if(old_)SelectObject(mem_,old_);if(bmp_)DeleteObject(bmp_);if(mem_)DeleteDC(mem_);if(screen_)ReleaseDC(nullptr,screen_);}
    int w()const{return w_;}int h()const{return h_;}
    bool jpeg(std::vector<byte>&out){
      if(!BitBlt(mem_,0,0,w_,h_,screen_,0,0,SRCCOPY|CAPTUREBLT))return false;Gdiplus::Bitmap img(bmp_,nullptr);IStream*st=nullptr;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&st)))return false;
      Gdiplus::EncoderParameters ep{};ep.Count=1;ep.Parameter[0].Guid=Gdiplus::EncoderQuality;ep.Parameter[0].Type=Gdiplus::EncoderParameterValueTypeLong;ep.Parameter[0].NumberOfValues=1;ep.Parameter[0].Value=&quality_;
      auto rs=img.Save(st,&jpg_,&ep);if(rs!=Gdiplus::Ok){st->Release();return false;}HGLOBAL hg=nullptr;GetHGlobalFromStream(st,&hg);SIZE_T z=GlobalSize(hg);void*p=GlobalLock(hg);out.assign((byte*)p,(byte*)p+z);GlobalUnlock(hg);st->Release();return true;
    }
};
static byte cursor_kind(){
    CURSORINFO ci{sizeof(ci)};if(!GetCursorInfo(&ci)||!(ci.flags&CURSOR_SHOWING))return 0;
    HCURSOR c=ci.hCursor;if(c==LoadCursor(nullptr,IDC_IBEAM))return 2;if(c==LoadCursor(nullptr,IDC_HAND))return 3;if(c==LoadCursor(nullptr,IDC_WAIT)||c==LoadCursor(nullptr,IDC_APPSTARTING))return 4;if(c==LoadCursor(nullptr,IDC_SIZEWE)||c==LoadCursor(nullptr,IDC_SIZENS)||c==LoadCursor(nullptr,IDC_SIZENWSE)||c==LoadCursor(nullptr,IDC_SIZENESW)||c==LoadCursor(nullptr,IDC_SIZEALL))return 5;return 1;
}
static void mouse_move(int x,int y){INPUT i{};i.type=INPUT_MOUSE;i.mi.dx=(LONG)((double)x*65535.0/std::max(1,GetSystemMetrics(SM_CXSCREEN)-1));i.mi.dy=(LONG)((double)y*65535.0/std::max(1,GetSystemMetrics(SM_CYSCREEN)-1));i.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;SendInput(1,&i,sizeof(i));}
static void mouse_button(byte b,bool d){DWORD f=0;if(b==0)f=d?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;else if(b==1)f=d?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_RIGHTUP;else if(b==2)f=d?MOUSEEVENTF_MIDDLEDOWN:MOUSEEVENTF_MIDDLEUP;if(f){INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=f;SendInput(1,&i,sizeof(i));}}
static void mouse_wheel(int d){INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=MOUSEEVENTF_WHEEL;i.mi.mouseData=d;SendInput(1,&i,sizeof(i));}
static void key_event(int vk,bool down){INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=(WORD)vk;i.ki.dwFlags=down?0:KEYEVENTF_KEYUP;SendInput(1,&i,sizeof(i));}

struct Config{
    int port=45900,fps=30,quality=95;bool audio=true,autostart=false;std::string password="123456";std::string hostid;
};
static std::string rndhex(size_t n=16){std::vector<byte>b(n);BCryptGenRandom(nullptr,b.data(),(ULONG)b.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG);static char h[]="0123456789abcdef";std::string s;s.resize(n*2);for(size_t i=0;i<n;i++){s[2*i]=h[b[i]>>4];s[2*i+1]=h[b[i]&15];}return s;}
static std::string extract(std::string_view j,std::string_view k,std::string def=""){auto p=j.find("\""+std::string(k)+"\"");if(p==std::string_view::npos)return def;p=j.find(':',p);if(p==std::string_view::npos)return def;p=j.find('"',p);if(p==std::string_view::npos)return def;auto e=j.find('"',p+1);if(e==std::string_view::npos)return def;return std::string(j.substr(p+1,e-p-1));}
static int extracti(std::string_view j,std::string_view k,int def){auto p=j.find("\""+std::string(k)+"\"");if(p==std::string_view::npos)return def;p=j.find(':',p);if(p==std::string_view::npos)return def;try{return std::stoi(std::string(j.substr(p+1)));}catch(...){return def;}}
static bool extractb(std::string_view j,std::string_view k,bool def){auto p=j.find("\""+std::string(k)+"\"");if(p==std::string_view::npos)return def;p=j.find(':',p);if(p==std::string_view::npos)return def;auto t=j.substr(p+1,8);if(t.find("true")!=std::string_view::npos)return true;if(t.find("false")!=std::string_view::npos)return false;return def;}
static Config load_cfg(){Config c;auto p=app_dir()/L"native-host.json";std::ifstream f(p,std::ios::binary);if(f){std::stringstream ss;ss<<f.rdbuf();auto j=ss.str();c.port=extracti(j,"port",45900);c.fps=extracti(j,"fps",30);c.quality=extracti(j,"quality",95);c.audio=extractb(j,"audio",true);c.autostart=extractb(j,"autostart",false);c.password=extract(j,"password","123456");c.hostid=extract(j,"hostid","");}if(c.hostid.empty())c.hostid=rndhex();return c;}
static void save_cfg(const Config&c){std::ofstream f(app_dir()/L"native-host.json",std::ios::binary|std::ios::trunc);f<<"{\"port\":"<<c.port<<",\"fps\":"<<c.fps<<",\"quality\":"<<c.quality<<",\"audio\":"<<(c.audio?"true":"false")<<",\"autostart\":"<<(c.autostart?"true":"false")<<",\"password\":\""<<c.password<<"\",\"hostid\":\""<<c.hostid<<"\"}";}

struct Seen{std::string id,name,ip;int port;Clock::time_point t;};
class Discovery{
    std::jthread tx_,rx_;std::atomic<bool>run_{false};int port_;std::string id_;std::mutex mu_;std::vector<Seen>seen_;
public:
    Discovery(int port,std::string id):port_(port),id_(std::move(id)){}
    ~Discovery(){stop();}
    void start(){
      if(run_.exchange(true))return;
      tx_=std::jthread([this](std::stop_token st){Sock s(socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP));if(!s)return;BOOL one=TRUE;setsockopt(s.s,SOL_SOCKET,SO_BROADCAST,(char*)&one,sizeof(one));sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(45901);a.sin_addr.s_addr=INADDR_BROADCAST;while(run_&&!st.stop_requested()){std::string m="SRD3|"+id_+"|"+wu8([]{wchar_t b[MAX_COMPUTERNAME_LENGTH+1]{};DWORD n=MAX_COMPUTERNAME_LENGTH+1;GetComputerNameW(b,&n);return std::wstring(b,n);}())+"|"+std::to_string(port_);sendto(s.s,m.data(),(int)m.size(),0,(sockaddr*)&a,sizeof(a));for(int i=0;i<20&&run_;++i)Sleep(100);}});
      rx_=std::jthread([this](std::stop_token st){Sock s(socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP));if(!s)return;BOOL one=TRUE;setsockopt(s.s,SOL_SOCKET,SO_REUSEADDR,(char*)&one,sizeof(one));sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=INADDR_ANY;a.sin_port=htons(45901);if(bind(s.s,(sockaddr*)&a,sizeof(a)))return;DWORD tv=500;setsockopt(s.s,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));char b[1024];while(run_&&!st.stop_requested()){sockaddr_in fr{};int fl=sizeof(fr);int n=recvfrom(s.s,b,sizeof(b)-1,0,(sockaddr*)&fr,&fl);if(n<=0)continue;b[n]=0;std::string m(b,n);if(m.rfind("SRD3|",0)!=0)continue;std::vector<std::string>v;size_t p=0;while(true){auto q=m.find('|',p);v.push_back(m.substr(p,q==std::string::npos?m.size()-p:q-p));if(q==std::string::npos)break;p=q+1;}if(v.size()<4||v[1]==id_)continue;char ip[64]{};inet_ntop(AF_INET,&fr.sin_addr,ip,sizeof ip);try{Seen x{v[1],v[2],ip,std::stoi(v[3]),Clock::now()};std::lock_guard lk(mu_);auto it=std::find_if(seen_.begin(),seen_.end(),[&](auto&z){return z.id==x.id&&z.ip==x.ip;});if(it==seen_.end())seen_.push_back(x);else *it=x;}catch(...){}}});
    }
    void stop(){run_=false;if(tx_.joinable()){tx_.request_stop();tx_.join();}if(rx_.joinable()){rx_.request_stop();rx_.join();}}
    std::vector<Seen> snapshot(){std::lock_guard lk(mu_);auto now=Clock::now();seen_.erase(std::remove_if(seen_.begin(),seen_.end(),[&](auto&s){return now-s.t>std::chrono::seconds(7);}),seen_.end());return seen_;}
};

class HostServer{
    Config cfg_;std::array<Sock,5>ls_;std::vector<std::jthread>ats_;std::atomic<bool>run_{false},session_{false};std::mutex sm_;std::string sip_;SOCKET ds_=INVALID_SOCKET,cs_=INVALID_SOCKET;
    void end(){std::lock_guard lk(sm_);session_=false;sip_.clear();if(ds_!=INVALID_SOCKET){shutdown(ds_,SD_BOTH);ds_=INVALID_SOCKET;}if(cs_!=INVALID_SOCKET){shutdown(cs_,SD_BOTH);cs_=INVALID_SOCKET;}}
    bool wait_ip(std::string ip){for(int i=0;i<50&&run_;++i){{std::lock_guard lk(sm_);if(session_&&sip_==ip)return true;}Sleep(50);}return false;}
    void data(Sock c){try{auto k=auth_server(c.s,cfg_.password);{std::lock_guard lk(sm_);if(session_)return;session_=true;sip_=peer_ip(c.s);ds_=c.s;}Channel ch(c.s,k,HOST_PREFIX,VIEWER_PREFIX);Capture cap(cfg_.quality);std::array<byte,8>d{};le32(d.data(),cap.w());le32(d.data()+4,cap.h());byte old=255;std::vector<byte>j;auto interval=std::chrono::microseconds(1000000/std::clamp(cfg_.fps,1,60));auto next=Clock::now();while(run_&&session_){if(!cap.jpeg(j))break;auto cur=cursor_kind();if(cur!=old){if(!ch.send(pkt::Cursor,std::span<const byte>(&cur,1)))break;old=cur;}if(!ch.send2(pkt::Screen,d,j))break;next+=interval;auto now=Clock::now();if(next>now)std::this_thread::sleep_until(next);else next=now;}}catch(...){log_line(L"data channel closed");}end();}
    void control(Sock c){auto ip=peer_ip(c.s);if(!wait_ip(ip))return;try{auto k=auth_server(c.s,cfg_.password);{std::lock_guard lk(sm_);cs_=c.s;}Channel ch(c.s,k,HOST_PREFIX,VIEWER_PREFIX);while(run_&&session_){auto p=ch.recv();if(!p)break;auto&b=p->payload;if(p->type==pkt::MouseMove&&b.size()>=8)mouse_move(rd32(b.data()),rd32(b.data()+4));else if(p->type==pkt::MouseButton&&b.size()>=2)mouse_button(b[0],b[1]!=0);else if(p->type==pkt::MouseWheel&&b.size()>=4)mouse_wheel(rd32(b.data()));else if(p->type==pkt::Key&&b.size()>=5)key_event(rd32(b.data()),b[4]!=0);else if(p->type==pkt::KeyCombination&&b.size()>=4){int n=rd32(b.data());if(n>0&&n<=16&&b.size()>=(size_t)(4+n*4)){std::vector<int>keys(n);for(int i=0;i<n;i++)keys[i]=rd32(b.data()+4+i*4);for(int v:keys)key_event(v,true);for(auto it=keys.rbegin();it!=keys.rend();++it)key_event(*it,false);}}}}catch(...){log_line(L"control channel closed");}end();}
    void preview(Sock c){try{auto k=auth_server(c.s,cfg_.password);Channel ch(c.s,k,HOST_PREFIX,VIEWER_PREFIX);Capture cap(70);std::vector<byte>j;if(cap.jpeg(j)){std::array<byte,8>d{};le32(d.data(),cap.w());le32(d.data()+4,cap.h());ch.send2(pkt::Preview,d,j);}}catch(...){}}
    void discard_auth(Sock c){try{(void)auth_server(c.s,cfg_.password);Sleep(100);}catch(...){}}
    void acceptor(int i){while(run_){sockaddr_storage a{};int z=sizeof(a);SOCKET x=accept(ls_[i].s,(sockaddr*)&a,&z);if(x==INVALID_SOCKET){if(run_)Sleep(50);continue;}tcp_opts(x);Sock c(x);if(i==0)std::thread([this,c=std::move(c)]()mutable{data(std::move(c));}).detach();else if(i==1)std::thread([this,c=std::move(c)]()mutable{control(std::move(c));}).detach();else if(i==2)std::thread([this,c=std::move(c)]()mutable{preview(std::move(c));}).detach();else std::thread([this,c=std::move(c)]()mutable{discard_auth(std::move(c));}).detach();}}
public:
    explicit HostServer(Config c):cfg_(std::move(c)){}
    ~HostServer(){stop();}
    bool start(){if(run_.exchange(true))return true;for(int i=0;i<5;i++){ls_[i]=listen_tcp(cfg_.port+i);if(!ls_[i]){stop();return false;}}for(int i=0;i<5;i++)ats_.emplace_back([this,i](std::stop_token){acceptor(i);});return true;}
    void stop(){if(!run_.exchange(false))return;end();for(auto&x:ls_)x.close();for(auto&t:ats_)if(t.joinable()){t.request_stop();t.join();}ats_.clear();}
    bool running()const{return run_;}void disconnect(){end();}
};

class Client{
    Sock data_,ctrl_;std::unique_ptr<Channel>dc_,cc_;std::vector<std::jthread>ths_;std::atomic<bool>run_{false};std::mutex qm_;std::condition_variable_any qcv_;struct M{byte t;std::vector<byte>p;};std::deque<M>q_;std::atomic<int>mx_{},my_{};std::atomic<bool>mp_{false};
    void rx(std::stop_token st){while(run_&&!st.stop_requested()){auto p=dc_->recv();if(!p)break;if(p->type==pkt::Screen&&p->payload.size()>8&&on_frame){int w=rd32(p->payload.data()),h=rd32(p->payload.data()+4);std::vector<byte>j(p->payload.begin()+8,p->payload.end());on_frame(std::move(j),w,h);}else if(p->type==pkt::Cursor&&!p->payload.empty())cursor=p->payload[0];}run_=false;if(on_close)on_close();}
    void tx(std::stop_token st){while(run_&&!st.stop_requested()){M m;bool has=false;{std::unique_lock lk(qm_);qcv_.wait_for(lk,st,std::chrono::milliseconds(30),[&]{return!q_.empty()||mp_.load()||!run_;});if(!q_.empty()){m=std::move(q_.front());q_.pop_front();has=true;}}if(has&&!cc_->send(m.t,m.p))break;if(mp_.exchange(false)){std::array<byte,8>p{};le32(p.data(),mx_);le32(p.data()+4,my_);if(!cc_->send(pkt::MouseMove,p))break;}}}
public:
    byte cursor=1;std::function<void(std::vector<byte>,int,int)>on_frame;std::function<void()>on_close;
    ~Client(){close();}
    bool open(std::string host,int port,std::string pass){try{data_=connect_tcp(host,port);if(!data_)return false;auto dk=auth_client(data_.s,pass);dc_=std::make_unique<Channel>(data_.s,dk,VIEWER_PREFIX,HOST_PREFIX);ctrl_=connect_tcp(host,port+1);if(!ctrl_)return false;auto ck=auth_client(ctrl_.s,pass);cc_=std::make_unique<Channel>(ctrl_.s,ck,VIEWER_PREFIX,HOST_PREFIX);run_=true;ths_.emplace_back([this](std::stop_token s){rx(s);});ths_.emplace_back([this](std::stop_token s){tx(s);});return true;}catch(...){close();return false;}}
    void close(){bool was=run_.exchange(false);data_.close();ctrl_.close();qcv_.notify_all();for(auto&t:ths_)if(t.joinable()){t.request_stop();if(t.get_id()!=std::this_thread::get_id())t.join();else t.detach();}ths_.clear();dc_.reset();cc_.reset();if(was){}}
    bool running()const{return run_;}
    void mm(int x,int y){mx_=x;my_=y;mp_=true;qcv_.notify_one();}
    void mb(byte b,bool d){std::lock_guard lk(qm_);q_.push_back({pkt::MouseButton,{b,(byte)(d?1:0)}});qcv_.notify_one();}
    void mw(int d){std::vector<byte>p(4);le32(p.data(),d);std::lock_guard lk(qm_);q_.push_back({pkt::MouseWheel,std::move(p)});qcv_.notify_one();}
    void key(int v,bool d){std::vector<byte>p(5);le32(p.data(),v);p[4]=d;std::lock_guard lk(qm_);q_.push_back({pkt::Key,std::move(p)});qcv_.notify_one();}
};

static HFONT mkfont(int pt,bool bold=false){LOGFONTW lf{};lf.lfHeight=-MulDiv(pt,GetDeviceCaps(GetDC(nullptr),LOGPIXELSY),72);wcscpy_s(lf.lfFaceName,bold?L"Segoe UI Semibold":L"Segoe UI");lf.lfWeight=bold?FW_SEMIBOLD:FW_NORMAL;return CreateFontIndirectW(&lf);}
static void dark_title(HWND h){BOOL b=TRUE;DwmSetWindowAttribute(h,20,&b,sizeof(b));}
static void fill(HDC dc,const RECT&r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
static void txt(HDC dc,const wchar_t*s,RECT r,COLORREF c,HFONT f,UINT fmt=DT_LEFT|DT_VCENTER|DT_SINGLELINE){SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);auto o=SelectObject(dc,f);DrawTextW(dc,s,-1,&r,fmt);SelectObject(dc,o);}
static void roundbox(HDC dc,RECT r,COLORREF c,COLORREF border,int rad=14){HBRUSH b=CreateSolidBrush(c);HPEN p=CreatePen(PS_SOLID,1,border);auto ob=SelectObject(dc,b),op=SelectObject(dc,p);RoundRect(dc,r.left,r.top,r.right,r.bottom,rad,rad);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(p);DeleteObject(b);}

static HINSTANCE GH;static constexpr UINT WM_TRAY=WM_APP+10, WM_FRAME=WM_APP+11, WM_SEEN=WM_APP+12;
class App;
static App* GAPP=nullptr;

class App{
public:
    Config cfg=load_cfg();std::unique_ptr<HostServer>host;std::unique_ptr<Discovery>disc;std::unique_ptr<Client>client;HWND hw_host{},hw_view{};NOTIFYICONDATAW tray{};HICON icon{};HFONT f9{},f10{},f12{},f15{};std::mutex fm;HBITMAP frame{};int fw{},fh{};std::string target_ip;int target_port=45900;std::string target_pass="123456";
    App(){f9=mkfont(9);f10=mkfont(10);f12=mkfont(12,true);f15=mkfont(15,true);icon=(HICON)LoadImageW(GH,MAKEINTRESOURCEW(1),IMAGE_ICON,32,32,LR_DEFAULTCOLOR);host=std::make_unique<HostServer>(cfg);host->start();disc=std::make_unique<Discovery>(cfg.port,cfg.hostid);disc->start();}
    ~App(){if(client)client->close();if(disc)disc->stop();if(host)host->stop();if(frame)DeleteObject(frame);DeleteObject(f9);DeleteObject(f10);DeleteObject(f12);DeleteObject(f15);}
    void setup_tray(){tray.cbSize=sizeof(tray);tray.hWnd=hw_host;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=WM_TRAY;tray.hIcon=icon;wcscpy_s(tray.szTip,L"Simple Remote Desk");Shell_NotifyIconW(NIM_ADD,&tray);}
    void show_view(){ShowWindow(hw_view,SW_SHOWMAXIMIZED);SetForegroundWindow(hw_view);}
    void connect_to(std::string ip,int port,std::string pass){
      if(client)client->close();target_ip=ip;target_port=port;target_pass=pass;client=std::make_unique<Client>();
      client->on_frame=[this](std::vector<byte>j,int w,int h){IStream*st=SHCreateMemStream(j.data(),(UINT)j.size());if(!st)return;Gdiplus::Bitmap im(st);HBITMAP hb=nullptr;if(im.GetLastStatus()==Gdiplus::Ok)im.GetHBITMAP(Gdiplus::Color(0,0,0),&hb);st->Release();if(hb){{std::lock_guard lk(fm);if(frame)DeleteObject(frame);frame=hb;fw=w;fh=h;}PostMessageW(hw_view,WM_FRAME,0,0);}};
      client->on_close=[this]{PostMessageW(hw_view,WM_FRAME,1,0);};
      if(client->open(ip,port,pass)){InvalidateRect(hw_view,nullptr,TRUE);}else MessageBoxW(hw_view,L"Не удалось подключиться. Проверьте адрес и пароль.",L"Simple Remote Desk",MB_ICONERROR);
    }
};
static std::wstring gettxt(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n,L'\0');GetWindowTextW(h,s.data(),n+1);return s;}
static LRESULT CALLBACK hostproc(HWND h,UINT m,WPARAM w,LPARAM l){
    static HWND eport,epass,efps,equal,start,viewer;
    switch(m){
    case WM_CREATE:{GAPP->hw_host=h;dark_title(h);GAPP->setup_tray();eport=CreateWindowExW(0,L"EDIT",std::to_wstring(GAPP->cfg.port).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,305,258,180,34,h,(HMENU)101,GH,nullptr);epass=CreateWindowExW(0,L"EDIT",u8w(GAPP->cfg.password).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_PASSWORD,665,258,245,34,h,(HMENU)102,GH,nullptr);efps=CreateWindowExW(0,L"EDIT",std::to_wstring(GAPP->cfg.fps).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,305,520,180,34,h,(HMENU)103,GH,nullptr);equal=CreateWindowExW(0,L"EDIT",std::to_wstring(GAPP->cfg.quality).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,305,578,180,34,h,(HMENU)104,GH,nullptr);start=CreateWindowExW(0,L"BUTTON",L"Перезапустить Host",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,680,735,190,42,h,(HMENU)105,GH,nullptr);viewer=CreateWindowExW(0,L"BUTTON",L"Открыть Viewer",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,875,735,160,42,h,(HMENU)106,GH,nullptr);for(HWND x:{eport,epass,efps,equal,start,viewer})SendMessageW(x,WM_SETFONT,(WPARAM)GAPP->f10,TRUE);return 0;}
    case WM_COMMAND:if(LOWORD(w)==106){GAPP->show_view();return 0;}if(LOWORD(w)==105){try{GAPP->cfg.port=std::stoi(gettxt(eport));GAPP->cfg.password=wu8(gettxt(epass));GAPP->cfg.fps=std::clamp(std::stoi(gettxt(efps)),1,60);GAPP->cfg.quality=std::clamp(std::stoi(gettxt(equal)),20,100);save_cfg(GAPP->cfg);GAPP->disc->stop();GAPP->host->stop();GAPP->host=std::make_unique<HostServer>(GAPP->cfg);if(!GAPP->host->start())MessageBoxW(h,L"Не удалось открыть сетевые порты.",L"Host",MB_ICONERROR);GAPP->disc=std::make_unique<Discovery>(GAPP->cfg.port,GAPP->cfg.hostid);GAPP->disc->start();InvalidateRect(h,nullptr,TRUE);}catch(...){MessageBoxW(h,L"Проверьте значения параметров.",L"Host",MB_ICONWARNING);}return 0;}break;
    case WM_TRAY:if(l==WM_LBUTTONDBLCLK||l==WM_LBUTTONUP){ShowWindow(h,SW_RESTORE);SetForegroundWindow(h);}else if(l==WM_RBUTTONUP){HMENU q=CreatePopupMenu();AppendMenuW(q,MF_STRING,1,L"Открыть Viewer");AppendMenuW(q,MF_STRING,2,L"Настройки Host");AppendMenuW(q,MF_SEPARATOR,0,nullptr);AppendMenuW(q,MF_STRING,3,L"Выход");POINT p;GetCursorPos(&p);SetForegroundWindow(h);int x=TrackPopupMenu(q,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(q);if(x==1)GAPP->show_view();if(x==2){ShowWindow(h,SW_RESTORE);SetForegroundWindow(h);}if(x==3)PostQuitMessage(0);}return 0;
    case WM_CLOSE:ShowWindow(h,SW_HIDE);return 0;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);fill(dc,cr,C_BG);RECT hd{0,0,cr.right,106};fill(dc,hd,C_HEADER);txt(dc,L"Simple Remote Host",{34,20,500,56},C_TEXT,GAPP->f15);txt(dc,L"Удалённый доступ к этому компьютеру",{34,57,600,86},C_MUTED,GAPP->f10);roundbox(dc,{24,126,cr.right-24,218},C_SURFACE,C_BORDER_SOFT);txt(dc,GAPP->host->running()?L"Host запущен":L"Host остановлен",{54,145,420,176},GAPP->host->running()?C_SUCCESS:C_OFFLINE,GAPP->f15);txt(dc,L"Ожидание подключений",{54,177,500,202},C_MUTED,GAPP->f9);roundbox(dc,{24,232,cr.right-24,450},C_SURFACE,C_BORDER_SOFT);txt(dc,L"Доступ и безопасность",{50,244,430,280},C_TEXT,GAPP->f12);txt(dc,L"Базовый порт",{50,302,270,336},C_MUTED,GAPP->f10);txt(dc,L"Пароль доступа",{540,302,760,336},C_MUTED,GAPP->f10);txt(dc,L"LAN и Radmin/VPN определяются в фоне; UI не перечисляет адаптеры по таймеру.",{50,372,990,410},C_MUTED2,GAPP->f9);roundbox(dc,{24,470,cr.right-24,690},C_SURFACE,C_BORDER_SOFT);txt(dc,L"Качество соединения",{50,482,430,518},C_TEXT,GAPP->f12);txt(dc,L"Кадры в секунду (FPS)",{50,522,270,556},C_MUTED,GAPP->f10);txt(dc,L"Качество JPEG (%)",{50,580,270,614},C_MUTED,GAPP->f10);txt(dc,L"Native C++20 • SRD3 • AES-GCM • Winsock",{540,545,990,580},C_MUTED,GAPP->f10);EndPaint(h,&ps);return 0;}
    case WM_DESTROY:return 0;
    }return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK viewproc(HWND h,UINT m,WPARAM w,LPARAM l){
    static HWND eip,eport,epass,connectb;
    switch(m){
    case WM_CREATE:{GAPP->hw_view=h;dark_title(h);eip=CreateWindowExW(0,L"EDIT",L"127.0.0.1",WS_CHILD|WS_VISIBLE|WS_BORDER,300,38,190,34,h,(HMENU)201,GH,nullptr);eport=CreateWindowExW(0,L"EDIT",L"45900",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,500,38,90,34,h,(HMENU)202,GH,nullptr);epass=CreateWindowExW(0,L"EDIT",L"123456",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_PASSWORD,600,38,170,34,h,(HMENU)203,GH,nullptr);connectb=CreateWindowExW(0,L"BUTTON",L"Подключиться",WS_CHILD|WS_VISIBLE,780,36,150,38,h,(HMENU)204,GH,nullptr);for(HWND x:{eip,eport,epass,connectb})SendMessageW(x,WM_SETFONT,(WPARAM)GAPP->f10,TRUE);SetTimer(h,1,1000,nullptr);return 0;}
    case WM_COMMAND:if(LOWORD(w)==204){try{GAPP->connect_to(wu8(gettxt(eip)),std::stoi(gettxt(eport)),wu8(gettxt(epass)));SetFocus(h);}catch(...){}}return 0;
    case WM_TIMER:InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_FRAME:InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_KEYDOWN:if(GAPP->client&&GAPP->client->running()){if(w==VK_F11){LONG s=GetWindowLongW(h,GWL_STYLE);SetWindowLongW(h,GWL_STYLE,s^WS_OVERLAPPEDWINDOW);ShowWindow(h,SW_MAXIMIZE);return 0;}GAPP->client->key((int)w,true);return 0;}break;
    case WM_KEYUP:if(GAPP->client&&GAPP->client->running()){GAPP->client->key((int)w,false);return 0;}break;
    case WM_MOUSEMOVE:if(GAPP->client&&GAPP->client->running()){RECT c;GetClientRect(h,&c);int y=GET_Y_LPARAM(l)-95;if(y>=0){int x=GET_X_LPARAM(l);int rw=GAPP->fw,rh=GAPP->fh;if(rw>0&&rh>0){RECT ar{0,95,c.right,c.bottom};double sc=std::min((double)(ar.right-ar.left)/rw,(double)(ar.bottom-ar.top)/rh);int dw=(int)(rw*sc),dh=(int)(rh*sc),ox=(c.right-dw)/2,oy=95+(c.bottom-95-dh)/2;if(x>=ox&&x<ox+dw&&GET_Y_LPARAM(l)>=oy&&GET_Y_LPARAM(l)<oy+dh)GAPP->client->mm((int)((x-ox)/sc),(int)((GET_Y_LPARAM(l)-oy)/sc));}}}return 0;}break;
    case WM_LBUTTONDOWN:if(GAPP->client&&GAPP->client->running()){SetFocus(h);GAPP->client->mb(0,true);return 0;}break;
    case WM_LBUTTONUP:if(GAPP->client&&GAPP->client->running()){GAPP->client->mb(0,false);return 0;}break;
    case WM_RBUTTONDOWN:if(GAPP->client&&GAPP->client->running()){GAPP->client->mb(1,true);return 0;}break;
    case WM_RBUTTONUP:if(GAPP->client&&GAPP->client->running()){GAPP->client->mb(1,false);return 0;}break;
    case WM_MOUSEWHEEL:if(GAPP->client&&GAPP->client->running()){GAPP->client->mw(GET_WHEEL_DELTA_WPARAM(w));return 0;}break;
    case WM_CLOSE:ShowWindow(h,SW_HIDE);return 0;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);fill(dc,cr,C_BG);fill(dc,{0,0,cr.right,94},C_HEADER);txt(dc,L"Simple Remote Viewer",{28,12,275,48},C_TEXT,GAPP->f15);txt(dc,L"Компьютеры",{28,50,180,82},C_ACCENT,GAPP->f10);bool conn=GAPP->client&&GAPP->client->running();if(conn){HBITMAP bm=nullptr;int rw=0,rh=0;{std::lock_guard lk(GAPP->fm);bm=GAPP->frame;rw=GAPP->fw;rh=GAPP->fh;}if(bm&&rw&&rh){HDC md=CreateCompatibleDC(dc);auto old=SelectObject(md,bm);BITMAP bi{};GetObject(bm,sizeof(bi),&bi);double sc=std::min((double)cr.right/rw,(double)(cr.bottom-94)/rh);int dw=(int)(rw*sc),dh=(int)(rh*sc),x=(cr.right-dw)/2,y=94+(cr.bottom-94-dh)/2;SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,x,y,dw,dh,md,0,0,bi.bmWidth,bi.bmHeight,SRCCOPY);SelectObject(md,old);DeleteDC(md);}else txt(dc,L"Подключение…",{0,95,cr.right,cr.bottom},C_MUTED,GAPP->f15,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}else{txt(dc,L"Мои компьютеры",{34,116,500,155},C_TEXT,GAPP->f15);txt(dc,L"Можно подключиться вручную или выбрать Host, найденный в локальной сети.",{34,154,900,185},C_MUTED,GAPP->f10);auto v=GAPP->disc->snapshot();int y=214;if(v.empty())txt(dc,L"Новые Host в локальной сети или Radmin VPN появятся здесь автоматически.",{48,y,900,y+45},C_MUTED2,GAPP->f10);for(auto&s:v){roundbox(dc,{36,y,650,y+104},C_SURFACE,C_BORDER_SOFT);txt(dc,u8w(s.name).c_str(),{58,y+10,380,y+38},C_TEXT,GAPP->f12);auto a=u8w(s.ip+":"+std::to_string(s.port));txt(dc,a.c_str(),{58,y+43,360,y+68},C_MUTED,GAPP->f9);txt(dc,L"Онлайн",{410,y+16,480,y+40},C_SUCCESS,GAPP->f9);y+=118;}}EndPaint(h,&ps);return 0;}
    }return DefWindowProcW(h,m,w,l);
}
static void classes(){
    WNDCLASSEXW a{sizeof(a)};a.hInstance=GH;a.hIcon=LoadIconW(GH,MAKEINTRESOURCEW(1));a.hCursor=LoadCursor(nullptr,IDC_ARROW);a.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);a.lpszClassName=L"SRD.Native.Host";a.lpfnWndProc=hostproc;RegisterClassExW(&a);
    WNDCLASSEXW b=a;b.lpszClassName=L"SRD.Native.Viewer";b.lpfnWndProc=viewproc;RegisterClassExW(&b);
}

int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR cmd,int){
    GH=h;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);Wsa wsa;GdiPlus gp;INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    HANDLE mx=CreateMutexW(nullptr,TRUE,L"Local\\SimpleRemoteDesk.Native");if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mx);return 0;}
    classes();App app;GAPP=&app;
    app.hw_host=CreateWindowExW(0,L"SRD.Native.Host",L"Simple Remote Host",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,1076,901,nullptr,nullptr,h,nullptr);
    app.hw_view=CreateWindowExW(0,L"SRD.Native.Viewer",L"Simple Remote Viewer",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,780,nullptr,nullptr,h,nullptr);
    bool autostart=wcsstr(cmd,L"--autostart")!=nullptr;bool settings=wcsstr(cmd,L"--settings")!=nullptr;
    if(settings)ShowWindow(app.hw_host,SW_SHOW);else if(!autostart)ShowWindow(app.hw_view,SW_SHOWMAXIMIZED);else ShowWindow(app.hw_host,SW_HIDE);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}Shell_NotifyIconW(NIM_DELETE,&app.tray);GAPP=nullptr;ReleaseMutex(mx);CloseHandle(mx);return 0;
}
