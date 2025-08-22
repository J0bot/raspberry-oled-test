#include <net/if.h>      // IFF_UP
#include <netdb.h>       // NI_MAXHOST
#include <arpa/inet.h>   // inet_ntop

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netdb.h>

#include <sys/utsname.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>


// ====== SSD1306 basics ======
static constexpr int W=128, H=64, PAGES=H/8;

static int i2c_cmd(int fd, uint8_t c){ uint8_t b[2]={0x00,c}; return write(fd,b,2)==2?0:-1; }
static int i2c_data(int fd, const uint8_t* d, size_t n){
    uint8_t tmp[128+1]; tmp[0]=0x40;
    size_t off=0;
    while(off<n){
        size_t k = std::min<size_t>(128, n-off);
        memcpy(tmp+1,d+off,k);
        if(write(fd,tmp,k+1)!=(ssize_t)(k+1)) return -1;
        off+=k;
    }
    return 0;
}
static int ssd1306_init(int fd){
    int r=0;
    r|=i2c_cmd(fd,0xAE);
    r|=i2c_cmd(fd,0x20); r|=i2c_cmd(fd,0x00); // horiz
    r|=i2c_cmd(fd,0xB0);
    r|=i2c_cmd(fd,0xC8);
    r|=i2c_cmd(fd,0x00); r|=i2c_cmd(fd,0x10);
    r|=i2c_cmd(fd,0x40);
    r|=i2c_cmd(fd,0x81); r|=i2c_cmd(fd,0x7F);
    r|=i2c_cmd(fd,0xA1);
    r|=i2c_cmd(fd,0xA6);
    r|=i2c_cmd(fd,0xA8); r|=i2c_cmd(fd,0x3F);
    r|=i2c_cmd(fd,0xA4);
    r|=i2c_cmd(fd,0xD3); r|=i2c_cmd(fd,0x00);
    r|=i2c_cmd(fd,0xD5); r|=i2c_cmd(fd,0x80);
    r|=i2c_cmd(fd,0xD9); r|=i2c_cmd(fd,0xF1);
    r|=i2c_cmd(fd,0xDA); r|=i2c_cmd(fd,0x12);
    r|=i2c_cmd(fd,0xDB); r|=i2c_cmd(fd,0x40);
    r|=i2c_cmd(fd,0x8D); r|=i2c_cmd(fd,0x14);
    r|=i2c_cmd(fd,0xAF);
    return r;
}
static int ssd1306_blit(int fd, const uint8_t* fb){
    for(int p=0;p<PAGES;++p){
        if(i2c_cmd(fd,0xB0+p)) return -1;
        if(i2c_cmd(fd,0x00))   return -1;
        if(i2c_cmd(fd,0x10))   return -1;
        if(i2c_data(fd, fb + p*W, W)) return -1;
    }
    return 0;
}

// ====== Framebuffer + tiny font 5x7 ======
struct FB{
    uint8_t b[W*PAGES];
    void clear(){ memset(b,0,sizeof(b)); }
    void px(int x,int y){
        if((unsigned)x>=W||(unsigned)y>=H) return;
        b[(y>>3)*W + x] |= (1u<<(y&7));
    }
    void hline(int x0,int x1,int y){ if(x0>x1) std::swap(x0,x1); for(int x=x0;x<=x1;x++) px(x,y); }
    void rect(int x,int y,int w,int h){ hline(x,x+w-1,y); hline(x,x+w-1,y+h-1); for(int yy=y;yy<y+h;yy++){ px(x,yy); px(x+w-1,yy);} }
};

// 5x7 font (ASCII 32..127)
static const uint8_t F[96][5]={
{0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},{36,42,127,42,18},{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},
{0,28,34,65,0},{0,65,34,28,0},{20,8,62,8,20},{8,8,62,8,8},{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},{32,16,8,4,2},
{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,73,77,51},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},
{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},{0,54,54,0,0},{0,86,54,0,0},{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},
{2,1,81,9,6},{50,73,121,65,62},{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},
{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},
{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{127,32,24,32,127},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},
{0,127,65,65,0},{2,4,8,16,32},{0,65,65,127,0},{4,2,1,2,4},{64,64,64,64,64},{0,1,2,0,0},{32,84,84,84,120},{127,72,68,68,56},
{56,68,68,68,32},{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},{12,82,82,82,62},{127,8,4,4,120},{0,68,125,64,0},
{32,64,64,61,0},{127,16,40,68,0},{0,65,127,64,0},{124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},{124,20,20,20,8},
{8,20,20,20,124},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},{60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},
{68,40,16,40,68},{12,80,80,80,60},{68,100,84,76,68},{0,8,54,65,0},{0,0,127,0,0},{0,65,54,8,0},{8,4,8,16,8}
};

static void drawChar(FB& fb,int x,int y,char c){
    if(c<32||c>127) return; const uint8_t* g=F[c-32];
    for(int i=0;i<5;i++){ uint8_t col=g[i]; for(int b=0;b<7;b++) if(col&(1<<b)) fb.px(x+i,y+b); }
}
static void drawText(FB& fb,int x,int y,const std::string& s){
    int cx=x; for(char c: s){ if(c=='\n'){ y+=8; cx=x; } else { drawChar(fb,cx,y,c); cx+=6; } }
}

// ====== System info ======
static std::string get_hostname(){
    struct utsname u; if(uname(&u)==0) return u.nodename; return "unknown";
}
static std::string get_ip_for(const char* ifn){
    struct ifaddrs *ifaddr=nullptr; if(getifaddrs(&ifaddr)==-1) return "-";
    std::string ip="-";
    for(struct ifaddrs* ifa=ifaddr; ifa; ifa=ifa->ifa_next){
        if(!ifa->ifa_addr || !(ifa->ifa_flags&IFF_UP)) continue;
        if(strcmp(ifa->ifa_name, ifn)!=0) continue;
        if(ifa->ifa_addr->sa_family==AF_INET){
            char host[NI_MAXHOST];
            auto* sin = (sockaddr_in*)ifa->ifa_addr;
            if(inet_ntop(AF_INET, &sin->sin_addr, host, sizeof(host))) { ip=host; break; }
        }
    }
    freeifaddrs(ifaddr);
    return ip;
}
static bool read_mem_kb(long& mem_total,long& mem_free){
    std::ifstream f("/proc/meminfo"); if(!f) return false;
    std::string k; long v; std::string unit;
    mem_total=mem_free=0;
    while(f>>k>>v>>unit){
        if(k=="MemTotal:") mem_total=v;
        else if(k=="MemAvailable:") { mem_free=v; break; }
    }
    return mem_total>0;
}
static std::string read_loadavg(){
    std::ifstream f("/proc/loadavg"); std::string a,b,c; if(f>>a>>b>>c) return a+" "+b+" "+c; return "-";
}
static std::string read_uptime(){
    std::ifstream f("/proc/uptime"); double up=0; if(f>>up){ int h=int(up/3600); int m=int((up - h*3600)/60); return std::to_string(h)+"h"+(m<10?"0":"")+std::to_string(m); } return "-";
}
// public IP via curl (timeout court). Retourne "N/A" si pas d’Internet.
static std::string get_public_ip(){
    FILE* fp = popen("curl -4 -m 1 -s https://ifconfig.me", "r");
    if(!fp) return "N/A";
    char buf[64]; std::string out; size_t n=fread(buf,1,sizeof(buf)-1,fp);
    if(n>0){ buf[n]=0; out=buf; }
    pclose(fp);
    // nettoyage
    if(out.size()>20) out.resize(20);
    for(char& c: out) if(c=='\n' || c=='\r') c=' ';
    return out.empty() ? "N/A" : out;
}

int main(int argc, char** argv){
    int addr = 0x3C; if(argc>1) addr = int(strtol(argv[1], nullptr, 0)); // ex: 0x3D

    int fd = open("/dev/i2c-1", O_RDWR);
    if(fd<0){ perror("open /dev/i2c-1"); return 1; }
    if(ioctl(fd, I2C_SLAVE, addr)<0){ perror("ioctl I2C_SLAVE"); return 1; }
    if(ssd1306_init(fd)!=0){ fprintf(stderr,"SSD1306 init fail\n"); return 1; }

    FB fb;
    const std::string host = get_hostname();

    while(true){
        // — collecte
        std::string ip_eth = get_ip_for("eth0");
        std::string ip_wlan = get_ip_for("wlan0");
        long mt=0, mf=0; read_mem_kb(mt,mf);
        std::string load = read_loadavg();
        std::string up = read_uptime();
        std::string pub = get_public_ip(); // 1s timeout, non bloquant long

        // — rendu
        fb.clear();
        // header
        drawText(fb, 2, 0, "BOXIONFETCH");
        fb.rect(0, 8, 127, 55);

        // lignes d'infos (6–7 lignes max sur 64px)
        int y=12;
        drawText(fb, 2,  y, "Host: " + host);               y+=8;
        drawText(fb, 2,  y, "Eth0: " + ip_eth);             y+=8;
        drawText(fb, 2,  y, "Wlan: " + ip_wlan);            y+=8;
        char memline[40];
        long used = (mt>0)? (mt - mf) : 0;
        snprintf(memline,sizeof(memline),"RAM : %ld/%ld MB", used/1024, mt/1024);
        drawText(fb, 2,  y, memline);                       y+=8;
        drawText(fb, 2,  y, "Load: " + load);               y+=8;
        drawText(fb, 2,  y, "Up  : " + up);                 y+=8;
        drawText(fb, 2,  y, "Pub : " + pub);

        // push
        if(ssd1306_blit(fd, fb.b)!=0){ fprintf(stderr,"blit fail\n"); break; }

        std::this_thread::sleep_for(std::chrono::milliseconds(900)); // ~1 Hz
    }

    close(fd);
    return 0;
}
