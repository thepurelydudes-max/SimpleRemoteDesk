#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <objidl.h>
#include <propidl.h>
#include <ole2.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <iphlpapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmsystem.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <cmath>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
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
#include <regex>
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

static std::string json_unescape(std::string_view s);
static std::string json_escape(std::string_view s);
static std::string b64_encode(std::span<const byte> d){
    if(d.empty())return{};DWORD n=0;CryptBinaryToStringA(d.data(),(DWORD)d.size(),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&n);
    std::string o(n? n-1:0,'\0');if(n)CryptBinaryToStringA(d.data(),(DWORD)d.size(),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,o.data(),&n);return o;
}
static std::vector<byte> b64_decode(std::string_view s){
    DWORD n=0;if(!CryptStringToBinaryA(s.data(),(DWORD)s.size(),CRYPT_STRING_BASE64,nullptr,&n,nullptr,nullptr))return{};
    std::vector<byte>o(n);if(!CryptStringToBinaryA(s.data(),(DWORD)s.size(),CRYPT_STRING_BASE64,o.data(),&n,nullptr,nullptr))return{};o.resize(n);return o;
}
static std::string dpapi_protect(std::string_view plain){
    if(plain.empty())return{};static const std::string ent="SimpleRemoteDesk-v2";
    DATA_BLOB in{(DWORD)plain.size(),(BYTE*)plain.data()},entropy{(DWORD)ent.size(),(BYTE*)ent.data()},out{};
    if(!CryptProtectData(&in,L"SimpleRemoteDesk", &entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return{};
    std::string r=b64_encode(std::span<const byte>(out.pbData,out.cbData));SecureZeroMemory((void*)plain.data(),0);LocalFree(out.pbData);return r;
}
static std::string dpapi_unprotect(std::string_view encoded){
    if(encoded.empty())return{};auto enc=b64_decode(encoded);if(enc.empty())return{};static const std::string ent="SimpleRemoteDesk-v2";
    DATA_BLOB in{(DWORD)enc.size(),enc.data()},entropy{(DWORD)ent.size(),(BYTE*)ent.data()},out{};
    if(!CryptUnprotectData(&in,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return{};
    std::string r((char*)out.pbData,out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return r;
}
static std::string rndhex(size_t n=16){std::vector<byte>b(n);BCryptGenRandom(nullptr,b.data(),(ULONG)b.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG);static char h[]="0123456789abcdef";std::string s;s.resize(n*2);for(size_t i=0;i<n;i++){s[2*i]=h[b[i]>>4];s[2*i+1]=h[b[i]&15];}return s;}
static std::string random_password(){
    static constexpr char chars[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";std::array<byte,12>b{};BCryptGenRandom(nullptr,b.data(),(ULONG)b.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::string s;s.reserve(b.size());for(byte x:b)s.push_back(chars[x%(sizeof(chars)-1)]);return s;
}
static std::string extract(std::string_view j,std::string_view k,std::string def=""){auto p=j.find("\""+std::string(k)+"\"");if(p==std::string_view::npos)return def;p=j.find(':',p);if(p==std::string_view::npos)return def;p=j.find('"',p);if(p==std::string_view::npos)return def;auto e=p+1;bool esc=false;for(;e<j.size();e++){if(!esc&&j[e]=='"')break;if(!esc&&j[e]=='\\')esc=true;else esc=false;}if(e>=j.size())return def;return json_unescape(j.substr(p+1,e-p-1));}
static int extracti(std::string_view j,std::string_view k,int def){auto p=j.find("\""+std::string(k)+"\"");if(p==std::string_view::npos)return def;p=j.find(':',p);if(p==std::string_view::npos)return def;try{return std::stoi(std::string(j.substr(p+1)));}catch(...){return def;}}
static bool extractb(std::string_view j,std::string_view k,bool def){auto p=j.find("\""+std::string(k)+"\"");if(p==std::string_view::npos)return def;p=j.find(':',p);if(p==std::string_view::npos)return def;auto t=j.substr(p+1,8);if(t.find("true")!=std::string_view::npos)return true;if(t.find("false")!=std::string_view::npos)return false;return def;}

struct Config{
    int port=45900,fps=30,quality=95;bool audio=true,autostart=false;std::string audio_device,password,hostid;
};
static void set_autostart(bool enabled){
    HKEY k{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS)return;
    if(enabled){wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,(DWORD)std::size(exe));std::wstring v=L"\""+std::wstring(exe)+L"\" --autostart";RegSetValueExW(k,L"SimpleRemoteDeskHost",0,REG_SZ,(BYTE*)v.c_str(),(DWORD)((v.size()+1)*sizeof(wchar_t)));}
    else RegDeleteValueW(k,L"SimpleRemoteDeskHost");RegCloseKey(k);
}
static Config load_cfg(){
    Config c;auto p=app_dir()/L"host.json";std::ifstream f(p,std::ios::binary);
    if(f){std::stringstream ss;ss<<f.rdbuf();auto j=ss.str();c.port=extracti(j,"Port",45900);c.fps=extracti(j,"Fps",30);c.quality=extracti(j,"JpegQuality",95);c.audio=extractb(j,"AudioEnabled",true);c.autostart=extractb(j,"AutoStartWindows",false);c.audio_device=extract(j,"AudioDeviceId","");c.hostid=extract(j,"HostId","");c.password=dpapi_unprotect(extract(j,"ProtectedPassword",""));}
    if(c.hostid.empty())c.hostid=rndhex();if(c.password.size()<6)c.password=random_password();c.port=std::clamp(c.port,1024,65531);c.fps=std::clamp(c.fps,1,60);c.quality=std::clamp(c.quality,20,100);return c;
}
static void save_cfg(const Config&c){
    ensure_dir(app_dir());std::ofstream f(app_dir()/L"host.json",std::ios::binary|std::ios::trunc);auto prot=dpapi_protect(c.password);
    f<<"{\n  \"Port\": "<<c.port<<",\n  \"ProtectedPassword\": \""<<json_escape(prot)<<"\",\n  \"Fps\": "<<c.fps<<",\n  \"JpegQuality\": "<<c.quality<<",\n  \"AudioEnabled\": "<<(c.audio?"true":"false")<<",\n  \"AudioDeviceId\": \""<<json_escape(c.audio_device)<<"\",\n  \"AutoStartWindows\": "<<(c.autostart?"true":"false")<<",\n  \"StartServerOnLaunch\": true,\n  \"HostId\": \""<<json_escape(c.hostid)<<"\",\n  \"SettingsVersion\": 4\n}\n";
    set_autostart(c.autostart);
}

using Microsoft::WRL::ComPtr;


static std::string path_u8(const std::filesystem::path& p){
    auto u=p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()),u.size());
}
static std::filesystem::path path_from_u8(std::string_view s){
    return std::filesystem::u8path(s.begin(),s.end());
}
static std::vector<std::filesystem::path> clipboard_files(){
    std::vector<std::filesystem::path> out;
    if(!OpenClipboard(nullptr)) return out;
    HANDLE h=GetClipboardData(CF_HDROP);
    if(h){
        HDROP d=(HDROP)h; UINT n=DragQueryFileW(d,0xFFFFFFFF,nullptr,0);
        for(UINT i=0;i<n&&out.size()<64;i++){
            UINT z=DragQueryFileW(d,i,nullptr,0); std::wstring w(z,L'\0');
            if(z){DragQueryFileW(d,i,w.data(),z+1);std::error_code ec;if(std::filesystem::exists(w,ec))out.emplace_back(w);}
        }
    }
    CloseClipboard(); return out;
}
static bool set_clipboard_files(const std::vector<std::filesystem::path>& files){
    if(files.empty()) return false;
    size_t chars=1; std::vector<std::wstring> ws; ws.reserve(files.size());
    for(auto&p:files){auto w=p.wstring();chars+=w.size()+1;ws.push_back(std::move(w));}
    SIZE_T bytes=sizeof(DROPFILES)+chars*sizeof(wchar_t);
    HGLOBAL hg=GlobalAlloc(GHND,bytes); if(!hg)return false;
    auto* base=(byte*)GlobalLock(hg); if(!base){GlobalFree(hg);return false;}
    auto* df=(DROPFILES*)base;df->pFiles=sizeof(DROPFILES);df->fWide=TRUE;
    wchar_t* q=(wchar_t*)(base+sizeof(DROPFILES));
    for(auto&w:ws){memcpy(q,w.c_str(),w.size()*sizeof(wchar_t));q+=w.size();*q++=L'\0';}
    *q=L'\0';GlobalUnlock(hg);
    for(int tries=0;tries<8;tries++){
        if(OpenClipboard(nullptr)){
            EmptyClipboard();
            if(SetClipboardData(CF_HDROP,hg)){CloseClipboard();return true;}
            CloseClipboard();break;
        }
        Sleep(20);
    }
    GlobalFree(hg);return false;
}
static void utf8_append_cp(std::string& o,unsigned cp){
    if(cp<=0x7f)o.push_back((char)cp);
    else if(cp<=0x7ff){o.push_back((char)(0xc0|(cp>>6)));o.push_back((char)(0x80|(cp&63)));}
    else if(cp<=0xffff){o.push_back((char)(0xe0|(cp>>12)));o.push_back((char)(0x80|((cp>>6)&63)));o.push_back((char)(0x80|(cp&63)));}
    else{o.push_back((char)(0xf0|(cp>>18)));o.push_back((char)(0x80|((cp>>12)&63)));o.push_back((char)(0x80|((cp>>6)&63)));o.push_back((char)(0x80|(cp&63)));}
}
static int hex4(std::string_view s,size_t p){
    int v=0;for(int i=0;i<4;i++){char c=s[p+i];int x=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(x<0)return-1;v=(v<<4)|x;}return v;
}
static std::string json_unescape(std::string_view s){
    std::string o;o.reserve(s.size());
    for(size_t i=0;i<s.size();i++){
        char c=s[i];if(c!='\\'||i+1>=s.size()){o.push_back(c);continue;}
        char e=s[++i];if(e=='"'||e=='\\'||e=='/')o.push_back(e);else if(e=='b')o.push_back('\b');else if(e=='f')o.push_back('\f');else if(e=='n')o.push_back('\n');else if(e=='r')o.push_back('\r');else if(e=='t')o.push_back('\t');
        else if(e=='u'&&i+4<s.size()){int u=hex4(s,i+1);i+=4;if(u>=0xD800&&u<=0xDBFF&&i+6<s.size()&&s[i+1]=='\\'&&s[i+2]=='u'){int lo=hex4(s,i+3);if(lo>=0xDC00&&lo<=0xDFFF){i+=6;utf8_append_cp(o,0x10000+((u-0xD800)<<10)+(lo-0xDC00));continue;}}if(u>=0)utf8_append_cp(o,(unsigned)u);}
    }return o;
}
static std::string json_escape(std::string_view s){
    static char hx[]="0123456789abcdef";std::string o;o.reserve(s.size()+16);
    for(unsigned char c:s){switch(c){case'"':o+="\\\"";break;case'\\':o+="\\\\";break;case'\b':o+="\\b";break;case'\f':o+="\\f";break;case'\n':o+="\\n";break;case'\r':o+="\\r";break;case'\t':o+="\\t";break;default:if(c<0x20){o+="\\u00";o.push_back(hx[c>>4]);o.push_back(hx[c&15]);}else o.push_back((char)c);}}return o;
}
struct FEntry{std::string rel;bool dir{};std::uint64_t len{};std::filesystem::path src;};
struct FManifest{std::vector<FEntry> entries;std::vector<std::string> roots;};

static std::string unique_root(std::string base,const std::vector<std::string>& used){
    if(base.empty())base="item";std::string r=base;int n=2;
    auto lower=[](std::string x){for(char&c:x)c=(char)tolower((unsigned char)c);return x;};
    while(std::any_of(used.begin(),used.end(),[&](auto&u){return lower(u)==lower(r);}))r=base+" ("+std::to_string(n++)+")";
    return r;
}
static FManifest build_manifest(const std::vector<std::filesystem::path>& roots){
    FManifest m;std::error_code ec;
    for(auto source:roots){
        if(m.roots.size()>=64)break;
        if(!std::filesystem::exists(source,ec))continue;
        auto base=path_u8(source.filename());auto root=unique_root(base,m.roots);m.roots.push_back(root);
        if(std::filesystem::is_regular_file(source,ec)){
            m.entries.push_back({root,false,(std::uint64_t)std::filesystem::file_size(source,ec),source});
        }else if(std::filesystem::is_directory(source,ec)){
            m.entries.push_back({root,true,0,source});
            std::filesystem::recursive_directory_iterator it(source,std::filesystem::directory_options::skip_permission_denied,ec),end;
            for(;it!=end&&!ec;++it){
                if(m.entries.size()>10000)throw std::runtime_error("too many files");
                auto rel=std::filesystem::relative(it->path(),source,ec);if(ec){ec.clear();continue;}
                std::string rp=root+"\\"+path_u8(rel);
                if(it->is_directory(ec))m.entries.push_back({rp,true,0,it->path()});
                else if(it->is_regular_file(ec))m.entries.push_back({rp,false,(std::uint64_t)it->file_size(ec),it->path()});
                ec.clear();
            }
        }
        if(m.entries.size()>10000)throw std::runtime_error("too many files");
    }
    return m;
}
static std::string manifest_json(const FManifest&m){
    std::ostringstream o;o<<"{\"Entries\":[";
    for(size_t i=0;i<m.entries.size();i++){auto&e=m.entries[i];if(i)o<<',';o<<"{\"RelativePath\":\""<<json_escape(e.rel)<<"\",\"IsDirectory\":"<<(e.dir?"true":"false")<<",\"Length\":"<<e.len<<"}";}
    o<<"],\"Roots\":[";for(size_t i=0;i<m.roots.size();i++){if(i)o<<',';o<<"\""<<json_escape(m.roots[i])<<"\"";}o<<"]}";return o.str();
}
static std::optional<std::string> json_field_string(std::string_view obj,std::string_view key){
    std::string needle="\""+std::string(key)+"\"";size_t p=obj.find(needle);if(p==std::string_view::npos)return{};p=obj.find(':',p+needle.size());if(p==std::string_view::npos)return{};p=obj.find('"',p+1);if(p==std::string_view::npos)return{};size_t b=++p;bool esc=false;for(;p<obj.size();p++){char c=obj[p];if(!esc&&c=='"')return json_unescape(obj.substr(b,p-b));if(!esc&&c=='\\')esc=true;else esc=false;}return{};
}
static bool json_field_bool(std::string_view obj,std::string_view key){
    auto p=obj.find("\""+std::string(key)+"\"");if(p==std::string_view::npos)return false;p=obj.find(':',p);if(p==std::string_view::npos)return false;return obj.substr(p+1,8).find("true")!=std::string_view::npos;
}
static std::uint64_t json_field_u64(std::string_view obj,std::string_view key){
    auto p=obj.find("\""+std::string(key)+"\"");if(p==std::string_view::npos)return 0;p=obj.find(':',p);if(p==std::string_view::npos)return 0;p++;while(p<obj.size()&&isspace((unsigned char)obj[p]))p++;std::uint64_t v=0;while(p<obj.size()&&isdigit((unsigned char)obj[p])){v=v*10+(obj[p++]-'0');}return v;
}
static FManifest parse_manifest(std::string_view j){
    FManifest m;size_t ep=j.find("\"Entries\""),ea=ep==std::string_view::npos?ep:j.find('[',ep),ee=ea;
    int depth=0;bool str=false,esc=false;if(ea!=std::string_view::npos)for(size_t i=ea+1;i<j.size();i++){char c=j[i];if(str){if(!esc&&c=='"')str=false;esc=!esc&&c=='\\';if(c!='\\')esc=false;continue;}if(c=='"'){str=true;continue;}if(c=='{'){if(depth++==0)ee=i;}else if(c=='}'&&--depth==0){auto obj=j.substr(ee,i-ee+1);auto rel=json_field_string(obj,"RelativePath");if(rel)m.entries.push_back({*rel,json_field_bool(obj,"IsDirectory"),json_field_u64(obj,"Length"),{}});}else if(c==']'&&depth==0)break;}
    size_t rp=j.find("\"Roots\""),ra=rp==std::string_view::npos?rp:j.find('[',rp),re=ra==std::string_view::npos?ra:j.find(']',ra);if(ra!=std::string_view::npos&&re!=std::string_view::npos){size_t p=ra+1;while(p<re){p=j.find('"',p);if(p==std::string_view::npos||p>=re)break;size_t b=++p;bool e=false;for(;p<re;p++){if(!e&&j[p]=='"'){m.roots.push_back(json_unescape(j.substr(b,p-b)));p++;break;}if(!e&&j[p]=='\\')e=true;else e=false;}}}
    if(m.entries.size()>10000)throw std::runtime_error("too many files");return m;
}
static std::filesystem::path safe_dest(const std::filesystem::path& root,std::string_view rel){
    auto rp=path_from_u8(rel);if(rp.is_absolute())throw std::runtime_error("absolute transfer path");
    for(auto&part:rp)if(part==L"..")throw std::runtime_error("path traversal");
    return root/rp;
}
static std::filesystem::path temp_transfer(std::wstring_view leaf){
    wchar_t b[MAX_PATH+2]{};DWORD n=GetTempPathW(MAX_PATH,b);std::filesystem::path p(n?std::wstring(b,n):L".");p/=L"SimpleRemoteDesk";p/=leaf;p/=u8w(rndhex());ensure_dir(p);return p;
}
static void send_manifest_files(Channel& ch,const FManifest&m){
    auto j=manifest_json(m);if(!ch.send(pkt::FileManifest,std::span<const byte>((const byte*)j.data(),j.size())))throw std::runtime_error("manifest send");
    std::vector<byte>buf(1024*1024+4);
    for(int i=0;i<(int)m.entries.size();i++){auto&e=m.entries[i];if(e.dir)continue;std::ifstream f(e.src,std::ios::binary);if(!f)continue;for(;;){f.read((char*)buf.data()+4,1024*1024);auto n=f.gcount();if(n<=0)break;le32(buf.data(),i);if(!ch.send(pkt::FileChunk,std::span<const byte>(buf.data(),(size_t)n+4)))throw std::runtime_error("chunk send");}}
    if(!ch.send(pkt::FileTransferEnd))throw std::runtime_error("end send");
}
static std::vector<std::filesystem::path> receive_manifest_files(Channel& ch,std::wstring_view folder){
    auto mp=ch.recv();if(!mp||mp->type!=pkt::FileManifest)throw std::runtime_error("manifest expected");
    FManifest m=parse_manifest(std::string_view((char*)mp->payload.data(),mp->payload.size()));auto root=temp_transfer(folder);
    for(auto&e:m.entries)if(e.dir)ensure_dir(safe_dest(root,e.rel));
    std::ofstream current;int ci=-1;
    for(;;){auto p=ch.recv();if(!p)throw std::runtime_error("transfer disconnected");if(p->type==pkt::FileTransferEnd)break;if(p->type!=pkt::FileChunk||p->payload.size()<4)continue;int idx=rd32(p->payload.data());if(idx<0||idx>=(int)m.entries.size())throw std::runtime_error("bad file index");auto&e=m.entries[idx];if(e.dir)continue;if(ci!=idx){if(current.is_open())current.close();auto dest=safe_dest(root,e.rel);ensure_dir(dest.parent_path());current.open(dest,std::ios::binary|std::ios::trunc);if(!current)throw std::runtime_error("file create");ci=idx;}current.write((char*)p->payload.data()+4,(std::streamsize)p->payload.size()-4);}
    if(current.is_open())current.close();std::vector<std::filesystem::path>top;for(auto&r:m.roots){auto p=safe_dest(root,r);std::error_code ec;if(std::filesystem::exists(p,ec))top.push_back(p);}return top;
}
static int remote_clipboard_get(std::string host,int port,std::string pass){
    Sock s=connect_tcp(host,port+4,8000);if(!s)return 0;auto k=auth_client(s.s,pass);Channel ch(s.s,k,VIEWER_PREFIX,HOST_PREFIX);if(!ch.send(pkt::FileClipboardGet))return 0;auto top=receive_manifest_files(ch,L"Clipboard");if(!top.empty())set_clipboard_files(top);return(int)top.size();
}
static int remote_clipboard_put(std::string host,int port,std::string pass,const std::vector<std::filesystem::path>&roots){
    auto m=build_manifest(roots);if(m.entries.empty())return 0;Sock s=connect_tcp(host,port+4,8000);if(!s)return 0;auto k=auth_client(s.s,pass);Channel ch(s.s,k,VIEWER_PREFIX,HOST_PREFIX);if(!ch.send(pkt::FileClipboardPut))return 0;send_manifest_files(ch,m);auto ack=ch.recv();if(!ack||ack->type!=pkt::FileTransferAck)throw std::runtime_error("no transfer ack");return(int)m.roots.size();
}



struct Profile{
    std::string id,hostid,name="Компьютер",host,pass;int port=45900;
};
static std::vector<Profile> load_profiles(){
    std::vector<Profile> out;std::ifstream f(app_dir()/L"viewer.json",std::ios::binary);if(!f)return out;std::stringstream ss;ss<<f.rdbuf();std::string j=ss.str();
    bool str=false,esc=false;int depth=0;size_t b=0;
    for(size_t i=0;i<j.size();i++){char c=j[i];if(str){if(!esc&&c=='"')str=false;if(!esc&&c=='\\')esc=true;else esc=false;continue;}if(c=='"'){str=true;continue;}if(c=='{'){if(depth++==0)b=i;}else if(c=='}'&&depth>0&&--depth==0){auto o=std::string_view(j).substr(b,i-b+1);Profile p;p.id=json_field_string(o,"Id").value_or(rndhex());p.hostid=json_field_string(o,"HostId").value_or("");p.name=json_field_string(o,"Name").value_or("Компьютер");p.host=json_field_string(o,"Host").value_or("");p.port=(int)json_field_u64(o,"Port");if(p.port<1024||p.port>65531)p.port=45900;p.pass=dpapi_unprotect(json_field_string(o,"ProtectedPassword").value_or(""));if(!p.host.empty())out.push_back(std::move(p));}}
    return out;
}
static void save_profiles(const std::vector<Profile>& ps){
    std::ofstream f(app_dir()/L"viewer.json",std::ios::binary|std::ios::trunc);f<<"[\n";
    for(size_t i=0;i<ps.size();i++){auto&p=ps[i];if(i)f<<",\n";auto prot=dpapi_protect(p.pass);f<<"  {\n    \"Id\": \""<<json_escape(p.id)<<"\",\n    \"HostId\": \""<<json_escape(p.hostid)<<"\",\n    \"Name\": \""<<json_escape(p.name)<<"\",\n    \"Host\": \""<<json_escape(p.host)<<"\",\n    \"Port\": "<<p.port<<",\n    \"ProtectedPassword\": \""<<json_escape(prot)<<"\"\n  }";}
    f<<"\n]\n";
}
static std::filesystem::path thumb_dir(){auto p=app_dir()/L"thumbnails";ensure_dir(p);return p;}
static std::filesystem::path thumb_path(std::string_view id){return thumb_dir()/(u8w(std::string(id))+L".jpg");}
static bool fetch_preview(const Profile&p){
    try{
        Sock s=connect_tcp(p.host,p.port+2,5000);if(!s)return false;auto k=auth_client(s.s,p.pass);Channel ch(s.s,k,VIEWER_PREFIX,HOST_PREFIX);auto q=ch.recv();if(!q||q->type!=pkt::Preview||q->payload.size()<=8)return false;
        std::ofstream f(thumb_path(p.id),std::ios::binary|std::ios::trunc);f.write((char*)q->payload.data()+8,(std::streamsize)q->payload.size()-8);return(bool)f;
    }catch(...){return false;}
}
static HBITMAP load_image_file(const std::filesystem::path&p){
    if(!std::filesystem::exists(p))return nullptr;Gdiplus::Bitmap im(p.c_str());if(im.GetLastStatus()!=Gdiplus::Ok)return nullptr;HBITMAP h{};if(im.GetHBITMAP(Gdiplus::Color(0,0,0),&h)!=Gdiplus::Ok)return nullptr;return h;
}

struct AudioFmt { int rate=48000,bits=16,channels=2,encoding=1; };

static bool wave_is_float(WAVEFORMATEX* f){
    if(!f) return false;
    if(f->wFormatTag==WAVE_FORMAT_IEEE_FLOAT) return true;
    if(f->wFormatTag==WAVE_FORMAT_EXTENSIBLE){
        auto* e=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(f);
        return IsEqualGUID(e->SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return false;
}
static bool wave_is_pcm(WAVEFORMATEX* f){
    if(!f) return false;
    if(f->wFormatTag==WAVE_FORMAT_PCM) return true;
    if(f->wFormatTag==WAVE_FORMAT_EXTENSIBLE){
        auto* e=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(f);
        return IsEqualGUID(e->SubFormat,KSDATAFORMAT_SUBTYPE_PCM);
    }
    return false;
}

class LoopbackCapture {
    std::jthread th_;
    std::atomic<bool> run_{false};
public:
    ~LoopbackCapture(){ stop(); }
    bool start(std::function<void(AudioFmt)> onfmt,std::function<void(std::span<const byte>)> ondata){
        if(run_.exchange(true)) return true;
        th_=std::jthread([this,onfmt=std::move(onfmt),ondata=std::move(ondata)](std::stop_token st){
            CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            ComPtr<IMMDeviceEnumerator> en; ComPtr<IMMDevice> dev; ComPtr<IAudioClient> ac; ComPtr<IAudioCaptureClient> cap;
            WAVEFORMATEX* wf=nullptr;
            try{
                if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&en)))) throw 1;
                if(FAILED(en->GetDefaultAudioEndpoint(eRender,eMultimedia,&dev))) throw 2;
                if(FAILED(dev.As(&ac))) throw 3;
                if(FAILED(ac->GetMixFormat(&wf))) throw 4;
                AudioFmt fmt{(int)wf->nSamplesPerSec,16,(int)wf->nChannels,1};
                if(onfmt) onfmt(fmt);
                if(FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,1000000,0,wf,nullptr))) throw 5;
                if(FAILED(ac->GetService(IID_PPV_ARGS(&cap)))) throw 6;
                if(FAILED(ac->Start())) throw 7;
                std::vector<byte> out;
                while(run_&&!st.stop_requested()){
                    UINT32 packets=0;
                    if(FAILED(cap->GetNextPacketSize(&packets))) break;
                    if(!packets){ Sleep(4); continue; }
                    while(packets){
                        BYTE* data=nullptr; UINT32 frames=0; DWORD flags=0;
                        if(FAILED(cap->GetBuffer(&data,&frames,&flags,nullptr,nullptr))) break;
                        size_t samples=(size_t)frames*wf->nChannels;
                        out.resize(samples*2); short* dst=(short*)out.data();
                        if(flags&AUDCLNT_BUFFERFLAGS_SILENT) std::fill(dst,dst+samples,0);
                        else if(wave_is_float(wf)&&wf->wBitsPerSample==32){
                            float* src=(float*)data;
                            for(size_t i=0;i<samples;i++){float x=std::clamp(src[i],-1.0f,1.0f);dst[i]=(short)std::lrintf(x*32767.0f);}
                        }else if(wave_is_pcm(wf)&&wf->wBitsPerSample==16) memcpy(dst,data,samples*2);
                        else if(wave_is_pcm(wf)&&wf->wBitsPerSample==24){
                            for(size_t i=0;i<samples;i++){BYTE* p=data+i*3;int x=p[0]|(p[1]<<8)|(p[2]<<16);if(x&0x800000)x|=0xff000000;dst[i]=(short)(x>>8);}
                        }else if(wave_is_pcm(wf)&&wf->wBitsPerSample==32){
                            int* src=(int*)data;for(size_t i=0;i<samples;i++)dst[i]=(short)(src[i]>>16);
                        }else std::fill(dst,dst+samples,0);
                        cap->ReleaseBuffer(frames);
                        if(ondata&&!out.empty()) ondata(out);
                        if(FAILED(cap->GetNextPacketSize(&packets))) break;
                    }
                }
                ac->Stop();
            }catch(...){ log_line(L"WASAPI loopback stopped"); }
            if(wf) CoTaskMemFree(wf);
            run_=false; CoUninitialize();
        });
        return true;
    }
    void stop(){run_=false;if(th_.joinable()){th_.request_stop();th_.join();}}
};

struct WaveBlock { WAVEHDR h{}; std::vector<byte> data; explicit WaveBlock(size_t n=131072):data(n){} };
class WavePlayer {
    HWAVEOUT out_{}; WAVEFORMATEX fmt_{}; std::mutex mu_;
    std::deque<std::unique_ptr<WaveBlock>> free_,used_;
    static void CALLBACK done(HWAVEOUT,UINT msg,DWORD_PTR self,DWORD_PTR p,DWORD_PTR){
        if(msg==WOM_DONE&&self) reinterpret_cast<WavePlayer*>(self)->recycle((WAVEHDR*)p);
    }
    void recycle(WAVEHDR* h){
        std::lock_guard lk(mu_);
        for(auto it=used_.begin();it!=used_.end();++it) if(&(*it)->h==h){
            waveOutUnprepareHeader(out_,h,sizeof(WAVEHDR));free_.push_back(std::move(*it));used_.erase(it);break;
        }
    }
public:
    ~WavePlayer(){close();}
    bool configure(AudioFmt f){
        close(); ZeroMemory(&fmt_,sizeof(fmt_));
        fmt_.wFormatTag=WAVE_FORMAT_PCM;fmt_.nChannels=(WORD)f.channels;fmt_.nSamplesPerSec=f.rate;fmt_.wBitsPerSample=16;
        fmt_.nBlockAlign=fmt_.nChannels*2;fmt_.nAvgBytesPerSec=fmt_.nSamplesPerSec*fmt_.nBlockAlign;
        if(waveOutOpen(&out_,WAVE_MAPPER,&fmt_,(DWORD_PTR)&done,(DWORD_PTR)this,CALLBACK_FUNCTION)!=MMSYSERR_NOERROR){out_=nullptr;return false;}
        for(int i=0;i<12;i++) free_.push_back(std::make_unique<WaveBlock>());
        return true;
    }
    void push(std::span<const byte> d){
        if(!out_||d.empty())return;std::unique_ptr<WaveBlock>b;
        {std::lock_guard lk(mu_);if(free_.empty())return;b=std::move(free_.front());free_.pop_front();}
        size_t n=std::min(d.size(),b->data.size());memcpy(b->data.data(),d.data(),n);ZeroMemory(&b->h,sizeof b->h);b->h.lpData=(LPSTR)b->data.data();b->h.dwBufferLength=(DWORD)n;
        if(waveOutPrepareHeader(out_,&b->h,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){std::lock_guard lk(mu_);free_.push_back(std::move(b));return;}
        auto* raw=b.get();{std::lock_guard lk(mu_);used_.push_back(std::move(b));}
        if(waveOutWrite(out_,&raw->h,sizeof(WAVEHDR))!=MMSYSERR_NOERROR) recycle(&raw->h);
    }
    void close(){
        if(!out_)return;waveOutReset(out_);Sleep(10);
        {std::lock_guard lk(mu_);for(auto&b:used_)waveOutUnprepareHeader(out_,&b->h,sizeof(WAVEHDR));used_.clear();free_.clear();}
        waveOutClose(out_);out_=nullptr;
    }
};

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
    void file(Sock c){
        try{
            auto k=auth_server(c.s,cfg_.password);Channel ch(c.s,k,HOST_PREFIX,VIEWER_PREFIX);auto cmd=ch.recv();if(!cmd)return;
            if(cmd->type==pkt::FileClipboardGet){
                auto m=build_manifest(clipboard_files());send_manifest_files(ch,m);
            }else if(cmd->type==pkt::FileClipboardPut){
                auto top=receive_manifest_files(ch,L"Incoming");set_clipboard_files(top);Sleep(180);
                key_event(VK_CONTROL,true);key_event('V',true);key_event('V',false);key_event(VK_CONTROL,false);
                static const byte ok[2]={'O','K'};ch.send(pkt::FileTransferAck,ok);
            }
        }catch(...){log_line(L"file transfer closed");}
    }
    void audio(Sock c){
        auto ip=peer_ip(c.s);if(!wait_ip(ip))return;
        try{
            auto k=auth_server(c.s,cfg_.password);Channel ch(c.s,k,HOST_PREFIX,VIEWER_PREFIX);
            if(!cfg_.audio){while(run_&&session_)Sleep(200);return;}
            std::mutex qmu;std::condition_variable cv;std::deque<std::vector<byte>>q;AudioFmt fmt{};std::atomic<bool>fmtready{false};
            LoopbackCapture cap;
            cap.start([&](AudioFmt f){fmt=f;fmtready=true;cv.notify_one();},[&](std::span<const byte>d){std::lock_guard lk(qmu);if(q.size()>=32)q.pop_front();q.emplace_back(d.begin(),d.end());cv.notify_one();});
            for(int n=0;n<100&&!fmtready&&run_&&session_;n++)Sleep(10);
            if(fmtready){
                std::array<byte,16>p{};le32(p.data(),fmt.rate);le32(p.data()+4,fmt.bits);le32(p.data()+8,fmt.channels);le32(p.data()+12,fmt.encoding);
                if(!ch.send(pkt::AudioFormat,p)){cap.stop();return;}
            }
            while(run_&&session_){
                std::vector<byte>b;{std::unique_lock lk(qmu);cv.wait_for(lk,std::chrono::milliseconds(100),[&]{return!q.empty()||!run_||!session_;});if(!q.empty()){b=std::move(q.front());q.pop_front();}}
                if(!b.empty()&&!ch.send(pkt::Audio,b))break;
            }
            cap.stop();
        }catch(...){log_line(L"audio channel closed");}
    }
    void acceptor(int i){while(run_){sockaddr_storage a{};int z=sizeof(a);SOCKET x=accept(ls_[i].s,(sockaddr*)&a,&z);if(x==INVALID_SOCKET){if(run_)Sleep(50);continue;}tcp_opts(x);Sock c(x);if(i==0)std::thread([this,c=std::move(c)]()mutable{data(std::move(c));}).detach();else if(i==1)std::thread([this,c=std::move(c)]()mutable{control(std::move(c));}).detach();else if(i==2)std::thread([this,c=std::move(c)]()mutable{preview(std::move(c));}).detach();else if(i==3)std::thread([this,c=std::move(c)]()mutable{audio(std::move(c));}).detach();else std::thread([this,c=std::move(c)]()mutable{file(std::move(c));}).detach();}}
public:
    explicit HostServer(Config c):cfg_(std::move(c)){}
    ~HostServer(){stop();}
    bool start(){if(run_.exchange(true))return true;for(int i=0;i<5;i++){ls_[i]=listen_tcp(cfg_.port+i);if(!ls_[i]){stop();return false;}}for(int i=0;i<5;i++)ats_.emplace_back([this,i](std::stop_token){acceptor(i);});return true;}
    void stop(){if(!run_.exchange(false))return;end();for(auto&x:ls_)x.close();for(auto&t:ats_)if(t.joinable()){t.request_stop();t.join();}ats_.clear();}
    bool running()const{return run_;}void disconnect(){end();}
};

class Client{
    Sock data_,ctrl_,audio_;std::unique_ptr<Channel>dc_,cc_,ac_;std::vector<std::jthread>ths_;std::atomic<bool>run_{false};std::mutex qm_;std::condition_variable_any qcv_;struct M{byte t;std::vector<byte>p;};std::deque<M>q_;std::atomic<int>mx_{},my_{};std::atomic<bool>mp_{false};WavePlayer player_;
    void rx(std::stop_token st){while(run_&&!st.stop_requested()){auto p=dc_->recv();if(!p)break;if(p->type==pkt::Screen&&p->payload.size()>8&&on_frame){int w=rd32(p->payload.data()),h=rd32(p->payload.data()+4);std::vector<byte>j(p->payload.begin()+8,p->payload.end());on_frame(std::move(j),w,h);}else if(p->type==pkt::Cursor&&!p->payload.empty())cursor=p->payload[0];}run_=false;if(on_close)on_close();}
    void tx(std::stop_token st){while(run_&&!st.stop_requested()){M m;bool has=false;{std::unique_lock lk(qm_);qcv_.wait_for(lk,st,std::chrono::milliseconds(30),[&]{return!q_.empty()||mp_.load()||!run_;});if(!q_.empty()){m=std::move(q_.front());q_.pop_front();has=true;}}if(has&&!cc_->send(m.t,m.p))break;if(mp_.exchange(false)){std::array<byte,8>p{};le32(p.data(),mx_);le32(p.data()+4,my_);if(!cc_->send(pkt::MouseMove,p))break;}}}
    void arx(std::stop_token st){while(run_&&!st.stop_requested()&&ac_){auto p=ac_->recv();if(!p)break;if(p->type==pkt::AudioFormat&&p->payload.size()>=16){AudioFmt f{rd32(p->payload.data()),rd32(p->payload.data()+4),rd32(p->payload.data()+8),rd32(p->payload.data()+12)};player_.configure(f);}else if(p->type==pkt::Audio)player_.push(p->payload);}}
public:
    byte cursor=1;std::function<void(std::vector<byte>,int,int)>on_frame;std::function<void()>on_close;
    ~Client(){close();}
    bool open(std::string host,int port,std::string pass){try{data_=connect_tcp(host,port);if(!data_)return false;auto dk=auth_client(data_.s,pass);dc_=std::make_unique<Channel>(data_.s,dk,VIEWER_PREFIX,HOST_PREFIX);ctrl_=connect_tcp(host,port+1);if(!ctrl_)return false;auto ck=auth_client(ctrl_.s,pass);cc_=std::make_unique<Channel>(ctrl_.s,ck,VIEWER_PREFIX,HOST_PREFIX);
      audio_=connect_tcp(host,port+3,2500);if(audio_){try{auto ak=auth_client(audio_.s,pass);ac_=std::make_unique<Channel>(audio_.s,ak,VIEWER_PREFIX,HOST_PREFIX);}catch(...){audio_.close();ac_.reset();}}
      run_=true;ths_.emplace_back([this](std::stop_token s){rx(s);});ths_.emplace_back([this](std::stop_token s){tx(s);});if(ac_)ths_.emplace_back([this](std::stop_token s){arx(s);});return true;}catch(...){close();return false;}}
    void close(){bool was=run_.exchange(false);data_.close();ctrl_.close();audio_.close();qcv_.notify_all();for(auto&t:ths_)if(t.joinable()){t.request_stop();if(t.get_id()!=std::this_thread::get_id())t.join();else t.detach();}ths_.clear();dc_.reset();cc_.reset();ac_.reset();player_.close();if(was){}}
    bool running()const{return run_;}
    void mm(int x,int y){mx_=x;my_=y;mp_=true;qcv_.notify_one();}
    void mb(byte b,bool d){std::lock_guard lk(qm_);q_.push_back({pkt::MouseButton,{b,(byte)(d?1:0)}});qcv_.notify_one();}
    void mw(int d){std::vector<byte>p(4);le32(p.data(),d);std::lock_guard lk(qm_);q_.push_back({pkt::MouseWheel,std::move(p)});qcv_.notify_one();}
    void key(int v,bool d){std::vector<byte>p(5);le32(p.data(),v);p[4]=d;std::lock_guard lk(qm_);q_.push_back({pkt::Key,std::move(p)});qcv_.notify_one();}
    void combo(std::initializer_list<int> keys){std::vector<byte>p(4+keys.size()*4);le32(p.data(),(int)keys.size());int i=0;for(int v:keys)le32(p.data()+4+(i++)*4,v);std::lock_guard lk(qm_);q_.push_back({pkt::KeyCombination,std::move(p)});qcv_.notify_one();}
};

static HFONT mkfont(int pt,bool bold=false){LOGFONTW lf{};lf.lfHeight=-MulDiv(pt,GetDeviceCaps(GetDC(nullptr),LOGPIXELSY),72);wcscpy_s(lf.lfFaceName,bold?L"Segoe UI Semibold":L"Segoe UI");lf.lfWeight=bold?FW_SEMIBOLD:FW_NORMAL;return CreateFontIndirectW(&lf);}
static void dark_title(HWND h){BOOL b=TRUE;DwmSetWindowAttribute(h,20,&b,sizeof(b));}
static void fill(HDC dc,const RECT&r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
static void txt(HDC dc,const wchar_t*s,RECT r,COLORREF c,HFONT f,UINT fmt=DT_LEFT|DT_VCENTER|DT_SINGLELINE){SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);auto o=SelectObject(dc,f);DrawTextW(dc,s,-1,&r,fmt);SelectObject(dc,o);}
static void roundbox(HDC dc,RECT r,COLORREF c,COLORREF border,int rad=14){HBRUSH b=CreateSolidBrush(c);HPEN p=CreatePen(PS_SOLID,1,border);auto ob=SelectObject(dc,b),op=SelectObject(dc,p);RoundRect(dc,r.left,r.top,r.right,r.bottom,rad,rad);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(p);DeleteObject(b);}



static HBRUSH surface2_brush(){static HBRUSH b=CreateSolidBrush(C_SURFACE2);return b;}
static void draw_owner_button(const DRAWITEMSTRUCT* di,bool primary=false,bool danger=false){
    RECT r=di->rcItem;bool pressed=(di->itemState&ODS_SELECTED)!=0;
    COLORREF bg=danger?(pressed?RGB(211,68,82):C_DANGER):(primary?(pressed?RGB(14,106,224):C_ACCENT):(pressed?RGB(20,41,68):C_SURFACE3));
    COLORREF br=primary||danger?bg:C_BORDER;fill(di->hDC,r,C_BG);roundbox(di->hDC,r,bg,br,10);
    wchar_t t[256]{};GetWindowTextW(di->hwndItem,t,255);HFONT f=(HFONT)SendMessageW(di->hwndItem,WM_GETFONT,0,0);
    txt(di->hDC,t,r,C_TEXT,f,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
static void draw_monitor(HDC dc,int x,int y,int w,int h,COLORREF c=C_MUTED){
    HPEN p=CreatePen(PS_SOLID,2,c);auto op=SelectObject(dc,p);HBRUSH b=(HBRUSH)GetStockObject(NULL_BRUSH);auto ob=SelectObject(dc,b);
    RoundRect(dc,x,y,x+w,y+h,4,4);MoveToEx(dc,x+w/2,y+h,nullptr);LineTo(dc,x+w/2,y+h+6);MoveToEx(dc,x+w/2-8,y+h+6,nullptr);LineTo(dc,x+w/2+8,y+h+6);
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(p);
}
struct ProfileDlgCtx{Profile* p{};bool isNew{},accepted{};HWND name{},host{},port{},pass{},show{};};
static std::wstring ctl_text(HWND h){int n=GetWindowTextLengthW(h);std::wstring w(n,L'\0');if(n)GetWindowTextW(h,w.data(),n+1);return w;}
static LRESULT CALLBACK profileproc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto* c=(ProfileDlgCtx*)GetWindowLongPtrW(h,GWLP_USERDATA);
    switch(m){
    case WM_NCCREATE:{auto*cs=(CREATESTRUCTW*)l;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)cs->lpCreateParams);return TRUE;}
    case WM_CREATE:{
        c=(ProfileDlgCtx*)((CREATESTRUCTW*)l)->lpCreateParams;dark_title(h);
        auto mk=[&](LPCWSTR cls,LPCWSTR text,DWORD style,int x,int y,int ww,int hh,int id){HWND q=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,ww,hh,h,(HMENU)(INT_PTR)id,GH,nullptr);SendMessageW(q,WM_SETFONT,(WPARAM)(HFONT)GetStockObject(DEFAULT_GUI_FONT),TRUE);return q;};
        c->name=mk(L"EDIT",u8w(c->p->name).c_str(),WS_BORDER|ES_AUTOHSCROLL,174,130,390,38,501);
        c->host=mk(L"EDIT",u8w(c->p->host).c_str(),WS_BORDER|ES_AUTOHSCROLL,174,184,390,38,502);
        c->port=mk(L"EDIT",std::to_wstring(c->p->port).c_str(),WS_BORDER|ES_NUMBER,174,238,210,38,503);
        c->pass=mk(L"EDIT",u8w(c->p->pass).c_str(),WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL,174,292,390,38,504);
        c->show=mk(L"BUTTON",L"Показать пароль",BS_AUTOCHECKBOX,174,340,190,30,505);
        mk(L"BUTTON",c->isNew?L"Добавить":L"Сохранить",BS_OWNERDRAW,216,406,156,48,1);
        mk(L"BUTTON",L"Отмена",BS_OWNERDRAW,384,406,156,48,2);
        return 0;
    }
    case WM_COMMAND:
        if(!c)break;
        if(LOWORD(w)==505){bool on=SendMessageW(c->show,BM_GETCHECK,0,0)==BST_CHECKED;SendMessageW(c->pass,EM_SETPASSWORDCHAR,on?0:L'●',0);InvalidateRect(c->pass,nullptr,TRUE);return 0;}
        if(LOWORD(w)==1){
            auto name=ctl_text(c->name),host=ctl_text(c->host),pass=ctl_text(c->pass);int port=0;try{port=std::stoi(ctl_text(c->port));}catch(...){}
            if(name.empty()||host.empty()||pass.size()<6||port<1024||port>65531){MessageBoxW(h,L"Укажите название, адрес, порт 1024–65531 и пароль не короче 6 символов.",L"Параметры подключения",MB_ICONINFORMATION);return 0;}
            c->p->name=wu8(name);c->p->host=wu8(host);c->p->port=port;c->p->pass=wu8(pass);if(c->p->id.empty())c->p->id=rndhex();c->accepted=true;DestroyWindow(h);return 0;
        }
        if(LOWORD(w)==2){DestroyWindow(h);return 0;}break;
    case WM_DRAWITEM:{auto*di=(DRAWITEMSTRUCT*)l;draw_owner_button(di,di->CtlID==1,false);return TRUE;}
    case WM_CTLCOLOREDIT:{HDC dc=(HDC)w;SetTextColor(dc,C_TEXT);SetBkColor(dc,C_SURFACE2);return(LRESULT)surface2_brush();}
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)w;SetTextColor(dc,C_MUTED);SetBkMode(dc,TRANSPARENT);return(LRESULT)GetStockObject(NULL_BRUSH);}
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);fill(dc,cr,C_BG);fill(dc,{0,0,cr.right,96},C_HEADER);draw_monitor(dc,26,27,42,34,C_ACCENT);txt(dc,c&&c->isNew?L"Добавить компьютер":L"Параметры подключения",{86,18,520,50},C_TEXT,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Укажите адрес Host и пароль доступа",{86,51,540,78},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));roundbox(dc,{24,108,cr.right-24,478},C_SURFACE,C_BORDER,14);
        txt(dc,L"Название",{52,128,160,166},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"IP / Host",{52,182,160,220},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Базовый порт",{52,236,160,274},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Пароль",{52,290,160,328},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));EndPaint(h,&ps);return 0;}
    case WM_CLOSE:DestroyWindow(h);return 0;
    }return DefWindowProcW(h,m,w,l);
}
static bool edit_profile(HWND owner,Profile&p,bool isNew){
    ProfileDlgCtx c{&p,isNew,false};RECT orc{};GetWindowRect(owner,&orc);int ww=620,hh=540,x=orc.left+(orc.right-orc.left-ww)/2,y=orc.top+(orc.bottom-orc.top-hh)/2;
    HWND h=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"SRD.Native.Profile",isNew?L"Добавить компьютер":L"Параметры подключения",WS_POPUP|WS_CAPTION|WS_SYSMENU,x,y,ww,hh,owner,nullptr,GH,&c);
    if(!h)return false;EnableWindow(owner,FALSE);ShowWindow(h,SW_SHOW);UpdateWindow(h);MSG msg;
    while(IsWindow(h)&&GetMessageW(&msg,nullptr,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    EnableWindow(owner,TRUE);SetForegroundWindow(owner);return c.accepted;
}
enum class HitAction{AddTop,Connect,Edit,Delete,AddSeen,Refresh,Disconnect};
struct ViewHit{RECT r{};HitAction action{};int index=-1;std::string id;};
static bool in_rect(RECT r,POINT p){return p.x>=r.left&&p.x<r.right&&p.y>=r.top&&p.y<r.bottom;}

static HINSTANCE GH;static constexpr UINT WM_TRAY=WM_APP+10, WM_FRAME=WM_APP+11, WM_SEEN=WM_APP+12;
class App;
static App* GAPP=nullptr;

class App{
public:
    Config cfg=load_cfg();std::atomic<bool> file_busy{false};std::unique_ptr<HostServer>host;std::unique_ptr<Discovery>disc;std::unique_ptr<Client>client;HWND hw_host{},hw_view{};NOTIFYICONDATAW tray{};HICON icon{};HFONT f9{},f10{},f12{},f15{};std::mutex fm,pm;HBITMAP frame{};int fw{},fh{};std::string target_ip;int target_port=45900;std::string target_pass="123456";std::vector<Profile> profiles;std::unordered_map<std::string,HBITMAP> thumbs;std::vector<ViewHit> hits;
    App(){f9=mkfont(9);f10=mkfont(10);f12=mkfont(12,true);f15=mkfont(15,true);profiles=load_profiles();icon=(HICON)LoadImageW(GH,MAKEINTRESOURCEW(1),IMAGE_ICON,32,32,LR_DEFAULTCOLOR);for(auto&p:profiles){if(auto h=load_image_file(thumb_path(p.id)))thumbs[p.id]=h;}host=std::make_unique<HostServer>(cfg);host->start();disc=std::make_unique<Discovery>(cfg.port,cfg.hostid);disc->start();}
    ~App(){if(client)client->close();if(disc)disc->stop();if(host)host->stop();if(frame)DeleteObject(frame);for(auto&[_,h]:thumbs)if(h)DeleteObject(h);DeleteObject(f9);DeleteObject(f10);DeleteObject(f12);DeleteObject(f15);}
    void setup_tray(){tray.cbSize=sizeof(tray);tray.hWnd=hw_host;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=WM_TRAY;tray.hIcon=icon;wcscpy_s(tray.szTip,L"Simple Remote Desk");Shell_NotifyIconW(NIM_ADD,&tray);}
    void show_view(){ShowWindow(hw_view,SW_SHOWMAXIMIZED);SetForegroundWindow(hw_view);}
    static int endpoint_score(const Seen&s){
        IN_ADDR a{};if(InetPtonA(AF_INET,s.ip.c_str(),&a)!=1)return 0;auto v=ntohl(a.S_un.S_addr);int b0=(v>>24)&255,b1=(v>>16)&255;
        if(b0==26)return 700;if(b0==100&&b1>=64&&b1<=127)return 650;if(b0==10||(b0==172&&b1>=16&&b1<=31)||(b0==192&&b1==168))return 800;return 200;
    }
    std::optional<Seen> online_for(const Profile&p){
        auto seen=disc->snapshot();std::optional<Seen> best;
        for(auto&s:seen){bool match=!p.hostid.empty()?s.id==p.hostid:s.ip==p.host;if(!match)continue;if(!best||endpoint_score(s)>endpoint_score(*best))best=s;}
        return best;
    }
    HBITMAP thumb_for(const Profile&p){std::lock_guard lk(pm);auto it=thumbs.find(p.id);return it==thumbs.end()?nullptr:it->second;}
    void refresh_thumb_async(Profile p){
        std::thread([this,p=std::move(p)]{if(!fetch_preview(p))return;HBITMAP h=load_image_file(thumb_path(p.id));if(!h)return;{std::lock_guard lk(pm);auto&slot=thumbs[p.id];if(slot)DeleteObject(slot);slot=h;}if(hw_view)PostMessageW(hw_view,WM_FRAME,0,0);}).detach();
    }
    void add_manual(){
        Profile p;p.id=rndhex();p.name="Новый компьютер";if(!edit_profile(hw_view,p,true))return;
        {std::lock_guard lk(pm);profiles.push_back(p);save_profiles(profiles);}refresh_thumb_async(p);InvalidateRect(hw_view,nullptr,TRUE);
    }
    void add_seen(const std::string&id){
        auto v=disc->snapshot();auto it=std::find_if(v.begin(),v.end(),[&](auto&s){return s.id==id;});if(it==v.end())return;
        Profile p;p.id=rndhex();p.hostid=it->id;p.name=it->name;p.host=it->ip;p.port=it->port;if(!edit_profile(hw_view,p,true))return;
        {std::lock_guard lk(pm);profiles.push_back(p);save_profiles(profiles);}refresh_thumb_async(p);InvalidateRect(hw_view,nullptr,TRUE);
    }
    void edit_saved(int idx){
        Profile p;{std::lock_guard lk(pm);if(idx<0||idx>=(int)profiles.size())return;p=profiles[idx];}
        if(!edit_profile(hw_view,p,false))return;{std::lock_guard lk(pm);if(idx<(int)profiles.size())profiles[idx]=p;save_profiles(profiles);}refresh_thumb_async(p);InvalidateRect(hw_view,nullptr,TRUE);
    }
    void delete_saved(int idx){
        Profile p;{std::lock_guard lk(pm);if(idx<0||idx>=(int)profiles.size())return;p=profiles[idx];}
        if(MessageBoxW(hw_view,(L"Удалить компьютер «"+u8w(p.name)+L"»?").c_str(),L"Simple Remote Desk",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
        {std::lock_guard lk(pm);if(idx<(int)profiles.size())profiles.erase(profiles.begin()+idx);auto it=thumbs.find(p.id);if(it!=thumbs.end()){if(it->second)DeleteObject(it->second);thumbs.erase(it);}save_profiles(profiles);}
        std::error_code ec;std::filesystem::remove(thumb_path(p.id),ec);InvalidateRect(hw_view,nullptr,TRUE);
    }
    void connect_saved(int idx){
        Profile p;{std::lock_guard lk(pm);if(idx<0||idx>=(int)profiles.size())return;p=profiles[idx];}
        if(auto o=online_for(p)){p.host=o->ip;p.port=o->port;{std::lock_guard lk(pm);if(idx<(int)profiles.size()){profiles[idx].host=p.host;profiles[idx].port=p.port;save_profiles(profiles);}}}
        connect_to(p.host,p.port,p.pass);
    }
    void copy_remote_files(){
      if(file_busy.exchange(true)||!client||!client->running())return;
      client->combo({VK_CONTROL,'C'});
      std::string ip=target_ip,pass=target_pass;int port=target_port;
      std::thread([this,ip=std::move(ip),pass=std::move(pass),port]{Sleep(650);try{remote_clipboard_get(ip,port,pass);}catch(...){log_line(L"remote clipboard download failed");}file_busy=false;}).detach();
    }
    bool paste_local_files(){
      auto files=clipboard_files();if(files.empty())return false;if(file_busy.exchange(true))return true;
      std::string ip=target_ip,pass=target_pass;int port=target_port;
      std::thread([this,ip=std::move(ip),pass=std::move(pass),port,files=std::move(files)]{try{remote_clipboard_put(ip,port,pass,files);}catch(...){log_line(L"remote clipboard upload failed");}file_busy=false;}).detach();return true;
    }
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
    case WM_CREATE:{GAPP->hw_host=h;dark_title(h);GAPP->setup_tray();eport=CreateWindowExW(0,L"EDIT",std::to_wstring(GAPP->cfg.port).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,305,258,180,34,h,(HMENU)101,GH,nullptr);epass=CreateWindowExW(0,L"EDIT",u8w(GAPP->cfg.password).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_PASSWORD,665,258,245,34,h,(HMENU)102,GH,nullptr);efps=CreateWindowExW(0,L"EDIT",std::to_wstring(GAPP->cfg.fps).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,305,520,180,34,h,(HMENU)103,GH,nullptr);equal=CreateWindowExW(0,L"EDIT",std::to_wstring(GAPP->cfg.quality).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,305,578,180,34,h,(HMENU)104,GH,nullptr);start=CreateWindowExW(0,L"BUTTON",L"Перезапустить Host",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,680,735,190,42,h,(HMENU)105,GH,nullptr);viewer=CreateWindowExW(0,L"BUTTON",L"Открыть Viewer",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,875,735,160,42,h,(HMENU)106,GH,nullptr);for(HWND x:{eport,epass,efps,equal,start,viewer})SendMessageW(x,WM_SETFONT,(WPARAM)(HFONT)GetStockObject(DEFAULT_GUI_FONT),TRUE);return 0;}
    case WM_COMMAND:if(LOWORD(w)==106){GAPP->show_view();return 0;}if(LOWORD(w)==105){try{GAPP->cfg.port=std::stoi(gettxt(eport));GAPP->cfg.password=wu8(gettxt(epass));GAPP->cfg.fps=std::clamp(std::stoi(gettxt(efps)),1,60);GAPP->cfg.quality=std::clamp(std::stoi(gettxt(equal)),20,100);save_cfg(GAPP->cfg);GAPP->disc->stop();GAPP->host->stop();GAPP->host=std::make_unique<HostServer>(GAPP->cfg);if(!GAPP->host->start())MessageBoxW(h,L"Не удалось открыть сетевые порты.",L"Host",MB_ICONERROR);GAPP->disc=std::make_unique<Discovery>(GAPP->cfg.port,GAPP->cfg.hostid);GAPP->disc->start();InvalidateRect(h,nullptr,TRUE);}catch(...){MessageBoxW(h,L"Проверьте значения параметров.",L"Host",MB_ICONWARNING);}return 0;}break;
    case WM_TRAY:if(l==WM_LBUTTONDBLCLK||l==WM_LBUTTONUP){ShowWindow(h,SW_RESTORE);SetForegroundWindow(h);}else if(l==WM_RBUTTONUP){HMENU q=CreatePopupMenu();AppendMenuW(q,MF_STRING,1,L"Открыть Viewer");AppendMenuW(q,MF_STRING,2,L"Настройки Host");AppendMenuW(q,MF_SEPARATOR,0,nullptr);AppendMenuW(q,MF_STRING,3,L"Выход");POINT p;GetCursorPos(&p);SetForegroundWindow(h);int x=TrackPopupMenu(q,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(q);if(x==1)GAPP->show_view();if(x==2){ShowWindow(h,SW_RESTORE);SetForegroundWindow(h);}if(x==3)PostQuitMessage(0);}return 0;
    case WM_CLOSE:ShowWindow(h,SW_HIDE);return 0;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);fill(dc,cr,C_BG);RECT hd{0,0,cr.right,106};fill(dc,hd,C_HEADER);txt(dc,L"Simple Remote Host",{34,20,500,56},C_TEXT,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Удалённый доступ к этому компьютеру",{34,57,600,86},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));roundbox(dc,{24,126,cr.right-24,218},C_SURFACE,C_BORDER_SOFT);txt(dc,GAPP->host->running()?L"Host запущен":L"Host остановлен",{54,145,420,176},GAPP->host->running()?C_SUCCESS:C_OFFLINE,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Ожидание подключений",{54,177,500,202},C_MUTED,GAPP->f9);roundbox(dc,{24,232,cr.right-24,450},C_SURFACE,C_BORDER_SOFT);txt(dc,L"Доступ и безопасность",{50,244,430,280},C_TEXT,GAPP->f12);txt(dc,L"Базовый порт",{50,302,270,336},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Пароль доступа",{540,302,760,336},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"LAN и Radmin/VPN определяются в фоне; UI не перечисляет адаптеры по таймеру.",{50,372,990,410},C_MUTED2,GAPP->f9);roundbox(dc,{24,470,cr.right-24,690},C_SURFACE,C_BORDER_SOFT);txt(dc,L"Качество соединения",{50,482,430,518},C_TEXT,GAPP->f12);txt(dc,L"Кадры в секунду (FPS)",{50,522,270,556},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Качество JPEG (%)",{50,580,270,614},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Native C++20 • SRD3 • AES-GCM • Winsock",{540,545,990,580},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));EndPaint(h,&ps);return 0;}
    case WM_DESTROY:return 0;
    }return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK viewproc(HWND h,UINT m,WPARAM w,LPARAM l){
    static HWND eip,eport,epass,connectb;
    switch(m){
    case WM_CREATE:{GAPP->hw_view=h;dark_title(h);eip=CreateWindowExW(0,L"EDIT",L"127.0.0.1",WS_CHILD|WS_VISIBLE|WS_BORDER,300,38,190,34,h,(HMENU)201,GH,nullptr);eport=CreateWindowExW(0,L"EDIT",L"45900",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,500,38,90,34,h,(HMENU)202,GH,nullptr);epass=CreateWindowExW(0,L"EDIT",L"123456",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_PASSWORD,600,38,170,34,h,(HMENU)203,GH,nullptr);connectb=CreateWindowExW(0,L"BUTTON",L"Подключиться",WS_CHILD|WS_VISIBLE,780,36,150,38,h,(HMENU)204,GH,nullptr);for(HWND x:{eip,eport,epass,connectb})SendMessageW(x,WM_SETFONT,(WPARAM)(HFONT)GetStockObject(DEFAULT_GUI_FONT),TRUE);SetTimer(h,1,1000,nullptr);return 0;}
    case WM_COMMAND:if(LOWORD(w)==204){try{GAPP->connect_to(wu8(gettxt(eip)),std::stoi(gettxt(eport)),wu8(gettxt(epass)));SetFocus(h);}catch(...){}}return 0;
    case WM_TIMER:InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_FRAME:InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_KEYDOWN:
        if(GAPP->client&&GAPP->client->running()){
            if(w==VK_F11){LONG s=GetWindowLongW(h,GWL_STYLE);SetWindowLongW(h,GWL_STYLE,s^WS_OVERLAPPEDWINDOW);ShowWindow(h,SW_MAXIMIZE);return 0;}
            bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            if(ctrl&&w=='C'){GAPP->copy_remote_files();return 0;}
            if(ctrl&&w=='V'){if(!GAPP->paste_local_files())GAPP->client->combo({VK_CONTROL,'V'});return 0;}
            if(ctrl&&shift&&w==VK_ESCAPE){GAPP->client->combo({VK_CONTROL,VK_SHIFT,VK_ESCAPE});return 0;}
            GAPP->client->key((int)w,true);return 0;
        }break;
    case WM_KEYUP:if(GAPP->client&&GAPP->client->running()){if(w=='C'||w=='V')return 0;GAPP->client->key((int)w,false);return 0;}break;
    case WM_SYSKEYDOWN:if(GAPP->client&&GAPP->client->running()&&w==VK_TAB&&(GetKeyState(VK_MENU)&0x8000)){GAPP->client->combo({VK_MENU,VK_TAB});return 0;}break;
    case WM_MOUSEMOVE:
        if(GAPP->client&&GAPP->client->running()){
            RECT c{}; GetClientRect(h,&c);
            int py=GET_Y_LPARAM(l);
            if(py>=95){
                int px=GET_X_LPARAM(l), rw=GAPP->fw, rh=GAPP->fh;
                if(rw>0&&rh>0){
                    double sc=std::min((double)c.right/rw,(double)(c.bottom-95)/rh);
                    int dw=(int)(rw*sc),dh=(int)(rh*sc),ox=(c.right-dw)/2,oy=95+(c.bottom-95-dh)/2;
                    if(px>=ox&&px<ox+dw&&py>=oy&&py<oy+dh)
                        GAPP->client->mm((int)((px-ox)/sc),(int)((py-oy)/sc));
                }
            }
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:if(GAPP->client&&GAPP->client->running()){SetFocus(h);GAPP->client->mb(0,true);return 0;}break;
    case WM_LBUTTONUP:if(GAPP->client&&GAPP->client->running()){GAPP->client->mb(0,false);return 0;}break;
    case WM_RBUTTONDOWN:if(GAPP->client&&GAPP->client->running()){GAPP->client->mb(1,true);return 0;}break;
    case WM_RBUTTONUP:if(GAPP->client&&GAPP->client->running()){GAPP->client->mb(1,false);return 0;}break;
    case WM_MOUSEWHEEL:if(GAPP->client&&GAPP->client->running()){GAPP->client->mw(GET_WHEEL_DELTA_WPARAM(w));return 0;}break;
    case WM_CLOSE:ShowWindow(h,SW_HIDE);return 0;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);fill(dc,cr,C_BG);fill(dc,{0,0,cr.right,94},C_HEADER);txt(dc,L"Simple Remote Viewer",{28,12,275,48},C_TEXT,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Компьютеры",{28,50,180,82},C_ACCENT,(HFONT)GetStockObject(DEFAULT_GUI_FONT));bool conn=GAPP->client&&GAPP->client->running();if(conn){HBITMAP bm=nullptr;int rw=0,rh=0;{std::lock_guard lk(GAPP->fm);bm=GAPP->frame;rw=GAPP->fw;rh=GAPP->fh;}if(bm&&rw&&rh){HDC md=CreateCompatibleDC(dc);auto old=SelectObject(md,bm);BITMAP bi{};GetObject(bm,sizeof(bi),&bi);double sc=std::min((double)cr.right/rw,(double)(cr.bottom-94)/rh);int dw=(int)(rw*sc),dh=(int)(rh*sc),x=(cr.right-dw)/2,y=94+(cr.bottom-94-dh)/2;SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,x,y,dw,dh,md,0,0,bi.bmWidth,bi.bmHeight,SRCCOPY);SelectObject(md,old);DeleteDC(md);}else txt(dc,L"Подключение…",{0,95,cr.right,cr.bottom},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}else{txt(dc,L"Мои компьютеры",{34,116,500,155},C_TEXT,(HFONT)GetStockObject(DEFAULT_GUI_FONT));txt(dc,L"Можно подключиться вручную или выбрать Host, найденный в локальной сети.",{34,154,900,185},C_MUTED,(HFONT)GetStockObject(DEFAULT_GUI_FONT));auto v=GAPP->disc->snapshot();int y=214;if(v.empty())txt(dc,L"Новые Host в локальной сети или Radmin VPN появятся здесь автоматически.",{48,y,900,y+45},C_MUTED2,(HFONT)GetStockObject(DEFAULT_GUI_FONT));for(auto&s:v){roundbox(dc,{36,y,650,y+104},C_SURFACE,C_BORDER_SOFT);txt(dc,u8w(s.name).c_str(),{58,y+10,380,y+38},C_TEXT,GAPP->f12);auto a=u8w(s.ip+":"+std::to_string(s.port));txt(dc,a.c_str(),{58,y+43,360,y+68},C_MUTED,GAPP->f9);txt(dc,L"Онлайн",{410,y+16,480,y+40},C_SUCCESS,GAPP->f9);y+=118;}}EndPaint(h,&ps);return 0;}
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
