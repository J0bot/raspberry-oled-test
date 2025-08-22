#include <QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QFont>
#include <QtGui/QPen>
#include <QtGui/QVector3D>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <cstdlib>

static constexpr int W = 128, H = 64, PAGES = H/8;

// ---- I2C / SSD1306 ----
static int i2c_cmd(int fd, uint8_t c){ uint8_t b[2]={0x00,c}; return write(fd,b,2)==2?0:-1; }
static int i2c_data(int fd, const uint8_t* d, size_t n){
    uint8_t tmp[17]; tmp[0]=0x40; size_t off=0;
    while(off<n){ size_t k=std::min<size_t>(16,n-off); memcpy(tmp+1,d+off,k);
        if(write(fd,tmp,k+1)!=(ssize_t)(k+1)) return -1; off+=k; }
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

// ---- Dither Bayer 4x4 → 1-bit ----
static const uint8_t B4[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
static void qimage_to_ssd1306_1bit(const QImage& src, uint8_t out[W*PAGES]){
    memset(out,0,W*PAGES);
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    for(int y=0;y<H;++y){
        const uint32_t* line = reinterpret_cast<const uint32_t*>(img.constScanLine(y));
        for(int x=0;x<W;++x){
            uint32_t px=line[x];
            uint8_t a=(px>>24)&0xFF, r=(px>>16)&0xFF, g=(px>>8)&0xFF, b=(px)&0xFF;
            int gray=int(0.299*r+0.587*g+0.114*b); gray=(gray*a)/255;
            int thr=B4[y&3][x&3]*16;
            if(gray>thr){ int page=y/8, bit=y%8; out[page*W+x]|=(1<<bit); }
        }
    }
}

// ---- Proj 3D simple → 2D ----
static QPointF proj(const QVector3D& v, float fov, float zcam){
    float z=v.z()+zcam; if(z<=0.1f) z=0.1f; float s=fov/z;
    return QPointF(W*0.5f + v.x()*s, H*0.5f - v.y()*s);
}

int main(int argc, char** argv){
    QGuiApplication app(argc, argv); // requis pour QPainter/QFont

    int addr = 0x3C;
    if(argc>1) addr = int(strtol(argv[1], nullptr, 0)); // accepte "0x3D" ou "61"

    // I2C
    int fd=open("/dev/i2c-1",O_RDWR);
    if(fd<0){ perror("open /dev/i2c-1"); return 1; }
    if(ioctl(fd,I2C_SLAVE,addr)<0){ perror("ioctl I2C_SLAVE"); return 1; }
    if(ssd1306_init(fd)!=0){ fprintf(stderr,"SSD1306 init fail\n"); return 1; }

    // Cube
    const float s=18.f;
    QVector3D V[]={
        {-s,-s,-s},{ s,-s,-s},{ s, s,-s},{-s, s,-s},
        {-s,-s, s},{ s,-s, s},{ s, s, s},{-s, s, s}
    };
    int E[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    float ang=0.f; uint8_t fb[W*PAGES];

    for(;;){
        QImage img(W,H,QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing,true);
        p.setPen(QColor(220,220,255));
        p.setFont(QFont("DejaVu Sans",10,QFont::Bold));
        p.drawText(4,12,QString("Qt 3D \u2192 OLED @0x%1").arg(QString::number(addr,16)));

        // rotation Rz*Ry
        float ca=std::cos(ang), sa=std::sin(ang);
        float cb=std::cos(ang*0.7f), sb=std::sin(ang*0.7f);
        auto rot=[&](const QVector3D& v){
            float x=v.x()*ca - v.y()*sa;
            float y=v.x()*sa + v.y()*ca;
            float z=v.z();
            float x2=x*cb + z*sb;
            float z2=-x*sb + z*cb;
            return QVector3D(x2,y,z2);
        };

        QPointF P[8];
        for(int i=0;i<8;++i) P[i]=proj(rot(V[i]), 90.f, 60.f);
        p.setPen(QColor(180,255,180));
        for(auto &e:E) p.drawLine(P[e[0]], P[e[1]]);
        p.end();

        qimage_to_ssd1306_1bit(img, fb);
        if(ssd1306_blit(fd, fb)!=0){ fprintf(stderr,"blit fail\n"); break; }

        ang += 0.06f;
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
    }
    close(fd);
    return 0;
}
