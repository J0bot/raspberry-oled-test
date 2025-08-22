#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/statvfs.h>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>

// ================= SSD1306 =================
static constexpr int W=128, H=64, PAGES=H/8;

static int i2c_cmd(int fd, uint8_t c){ uint8_t b[2]={0x00,c}; return write(fd,b,2)==2?0:-1; }
static int i2c_data(int fd, const uint8_t* d, size_t n){
    uint8_t tmp[128+1]; tmp[0]=0x40; size_t off=0;
    while(off<n){ size_t k = (n-off>128)?128:(n-off);
        memcpy(tmp+1,d+off,k);
        if(write(fd,tmp,k+1)!=(ssize_t)(k+1)) return -1;
        off+=k;
    }
    return 0;
}
static int ssd1306_init(int fd){
    int r=0;
    r|=i2c_cmd(fd,0xAE); r|=i2c_cmd(fd,0x20); r|=i2c_cmd(fd,0x00);
    r|=i2c_cmd(fd,0xB0); r|=i2c_cmd(fd,0xC8);
    r|=i2c_cmd(fd,0x00); r|=i2c_cmd(fd,0x10);
    r|=i2c_cmd(fd,0x40);
    r|=i2c_cmd(fd,0x81); r|=i2c_cmd(fd,0x7F);
    r|=i2c_cmd(fd,0xA1); r|=i2c_cmd(fd,0xA6);
    r|=i2c_cmd(fd,0xA8); r|=i2c_cmd(fd,0x3F);
    r|=i2c_cmd(fd,0xA4);
    r|=i2c_cmd(fd,0xD3); r|=i2c_cmd(fd,0x00);
    r|=i2c_cmd(fd,0xD5); r|=i2c_cmd(fd,0x80);
    r|=i2c_cmd(fd,0xD9); r|=i2c_cmd(fd,0xF1);
    r|=i2c_cmd(fd,0xDA); r|=i2c_cmd(fd,0x12);
    r|=i2c_cmd(fd,0xDB); r|=i2c_cmd(fd,0x40);
    r|=i2c_cmd(fd,0x8D); r|=i2c_cmd(fd,0x14);
    r|=i2c_cmd(fd,0xAF); return r;
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

// =========== Framebuffer + font 5x7 ===========
struct FB{ uint8_t b[W*PAGES]; void clear(){ memset(b,0,sizeof(b)); }
    void px(int x,int y){ if((unsigned)x>=W||(unsigned)y>=H) return; b[(y>>3)*W + x] |= (1u<<(y&7)); } };
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
static void txt(FB& fb,int x,int y,const std::string& s){
    int cx=x; for(char c: s){ if(c=='\n'){ y+=8; cx=x; continue; }
        if(c<32||c>127){ cx+=6; continue; } const uint8_t* g=F[c-32];
        for(int i=0;i<5;i++){ uint8_t col=g[i]; for(int b=0;b<7;b++) if(col&(1<<b)) fb.px(cx+i,y+b); }
        cx+=6; } }
static void rect(FB& fb,int x,int y,int w,int h){
    for(int xx=x;xx<x+w;xx++){ fb.px(xx,y); fb.px(xx,y+h-1); }
    for(int yy=y;yy<y+h;yy++){ fb.px(x,yy); fb.px(x+w-1,yy); }
}

// =============== Infos système ===============
static std::string ip_iface(const char* ifn){
    struct ifaddrs* ifaddr=nullptr; if(getifaddrs(&ifaddr)==-1) return "-";
    std::string ip="-";
    for(auto* a=ifaddr;a;a=a->ifa_next){
        if(!a->ifa_addr || strcmp(a->ifa_name,ifn)!=0) continue;
        if(a->ifa_addr->sa_family==AF_INET){
            char h[NI_MAXHOST]; auto* s=(sockaddr_in*)a->ifa_addr;
            if(inet_ntop(AF_INET,&s->sin_addr,h,sizeof(h))) { ip=h; break; }
        }
    }
    freeifaddrs(ifaddr); return ip;
}
static bool mem_kb(long& total,long& avail){
    std::ifstream f("/proc/meminfo"); if(!f) return false;
    std::string k; long v; std::string unit; total=avail=0;
    while(f>>k>>v>>unit){ if(k=="MemTotal:") total=v; else if(k=="MemAvailable:"){ avail=v; break; } }
    return total>0;
}
static void disk_root_gb(double& used,double& total){
    struct statvfs st{}; if(statvfs("/",&st)!=0){ used=total=0; return; }
    double b=st.f_frsize;
    total = (st.f_blocks*b)/(1024.0*1024.0*1024.0);
    double free = (st.f_bavail*b)/(1024.0*1024.0*1024.0);
    used = total - free;
}
static std::string uptime_str(){
    std::ifstream f("/proc/uptime"); double up=0; if(!(f>>up)) return "-";
    int h=int(up/3600), m=int((up-h*3600)/60);
    char buf[16]; snprintf(buf,sizeof(buf),"%dh%02d",h,m); return buf;
}
static std::string power_flags(){
    FILE* fp=popen("vcgencmd get_throttled 2>/dev/null","r");
    if(!fp) return "PWR:n/a";
    char buf[64]={0}; fread(buf,1,sizeof(buf)-1,fp); pclose(fp);
    unsigned val=0; if(sscanf(buf,"throttled=0x%x",&val)!=1) return "PWR:n/a";
    bool uv = (val&(1u<<0)) || (val&(1u<<16));
    bool th = (val&(1u<<1)) || (val&(1u<<17));
    bool cp = (val&(1u<<2)) || (val&(1u<<18));
    if(!uv && !th && !cp) return "PWR:ok";
    std::string s="PWR:"; if(uv) s+="UV "; if(th) s+="TH "; if(cp) s+="CAP"; return s;
}
// CPU% calc
static double get_cpu_usage(){
    static long long prevIdle=0, prevTotal=0;
    std::ifstream f("/proc/stat"); std::string cpu; long long u,n,s,i,io,irq,sirq,st=0;
    f>>cpu>>u>>n>>s>>i>>io>>irq>>sirq>>st; long long Idle=i+io; long long Total=u+n+s+Idle+irq+sirq+st;
    long long dT=Total-prevTotal, dI=Idle-prevIdle;
    prevTotal=Total; prevIdle=Idle; if(dT==0) return 0.0;
    return 100.0*(dT-dI)/dT;
}
static int get_temp(){
    std::ifstream f("/sys/class/thermal/thermal_zone0/temp");
    int t=0; f>>t; return t/1000;
}

int main(int argc,char** argv){
    int addr=0x3C; if(argc>1) addr=int(strtol(argv[1],nullptr,0)); // ex: 0x3D

    int fd=open("/dev/i2c-1",O_RDWR);
    if(fd<0){ perror("open /dev/i2c-1"); return 1; }
    if(ioctl(fd,I2C_SLAVE,addr)<0){ perror("ioctl I2C_SLAVE"); return 1; }
    if(ssd1306_init(fd)!=0){ fprintf(stderr,"SSD1306 init fail\n"); return 1; }

    FB fb;
    while(true){
        std::string ip_wifi = ip_iface("wlan0");
        long mt=0, ma=0; mem_kb(mt,ma);
        double du=0, dt=0; disk_root_gb(du,dt);
        std::string up = uptime_str();
        std::string pwr = power_flags();
        double cpu = get_cpu_usage();
        int temp = get_temp();

        fb.clear();
        txt(fb,2,0,"BOXION"); rect(fb,0,8,127,55);
        int y=12; char line[40];
        txt(fb,2,y,"WiFi:"+ip_wifi);            y+=8;
        long used=(mt>0)?(mt-ma):0;
        snprintf(line,sizeof(line),"RAM : %ld/%ldMB", used/1024, mt/1024);
        txt(fb,2,y,line);                      y+=8;
        snprintf(line,sizeof(line),"DISK: %.1f/%.1fG", du, dt);
        txt(fb,2,y,line);                      y+=8;
        txt(fb,2,y,"UP  :"+up);                y+=8;
        snprintf(line,sizeof(line),"CPU:%.0f%% T:%dC",cpu,temp);
        txt(fb,2,y,line);                      y+=8;
        txt(fb,2,y,pwr);

        if(ssd1306_blit(fd, fb.b)!=0){ fprintf(stderr,"blit fail\n"); break; }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    close(fd); return 0;
}
