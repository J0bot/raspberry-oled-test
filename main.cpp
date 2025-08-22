#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QFont>
#include <QtGui/QPen>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>

static constexpr int W = 128;
static constexpr int H = 64;
static constexpr int PAGES = H/8;

// --- I2C / SSD1306 ---
static int i2c_cmd(int fd, uint8_t c){
    uint8_t b[2] = {0x00, c};
    return write(fd,b,2)==2?0:-1;
}
static int i2c_data(int fd, const uint8_t* d, size_t n){
    uint8_t tmp[17]; tmp[0]=0x40;
    size_t off=0;
    while(off<n){
        size_t k = std::min<size_t>(16, n-off);
        memcpy(tmp+1, d+off, k);
        if(write(fd,tmp,k+1)!=(ssize_t)(k+1)) return -1;
        off+=k;
    }
    return 0;
}
static int ssd1306_init(int fd){
    int r=0;
    r|=i2c_cmd(fd,0xAE);
    r|=i2c_cmd(fd,0x20); r|=i2c_cmd(fd,0x00); // horiz addressing
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

// --- Dithering Bayer 4x4 en 1-bit ---
// Table 4x4 normalisée sur 0..16
static const uint8_t B4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5}
};

static void qimage_to_ssd1306_1bit(const QImage& src, uint8_t out[W*PAGES]){
    memset(out,0,W*PAGES);
    // Assure format ARGB32 pour accès rapide
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    for(int y=0;y<H;++y){
        const uint32_t* line = reinterpret_cast<const uint32_t*>(img.constScanLine(y));
        for(int x=0;x<W;++x){
            uint32_t px = line[x];
            uint8_t a = (px>>24)&0xFF;
            uint8_t r = (px>>16)&0xFF;
            uint8_t g = (px>> 8)&0xFF;
            uint8_t b = (px    )&0xFF;
            // Luma simple (BT.601 approx)
            int gray = (int)(0.299*r + 0.587*g + 0.114*b);
            gray = (gray * a)/255; // tient compte de l’alpha
            // seuil Bayer (0..255) ~ (B4*16)
            int threshold = B4[y&3][x&3] * 16;
            bool on = gray > threshold;
            if(on){
                int page = y/8, bit = y%8;
                out[page*W + x] |= (1<<bit);
            }
        }
    }
}

int main(){
    // 1) Crée une scène Qt hors-écran (aucun serveur X requis)
    QImage canvas(W, H, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);

    // “Couleurs” : visibles si tu affiches la même image via HDMI ; sur OLED ce sera ditheré.
    // Fond
    p.fillRect(0,0,W,H, QColor(10,10,10));

    // Titre
    QFont f("DejaVu Sans", 12, QFont::Bold);
    p.setFont(f);
    p.setPen(QColor(0,255,0)); // vert (sera converti)
    p.drawText(4, 14, "Hello Qt → OLED");

    // Sous-titre
    p.setFont(QFont("DejaVu Sans", 9));
    p.setPen(QColor(180,180,255));
    p.drawText(4, 28, "Dither Bayer 4x4 (mono)");

    // Quelques formes “colorées”
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,80,80));  p.drawEllipse(QPoint(100,18), 10, 6);
    p.setBrush(QColor(80,180,255)); p.drawRect(4, 36, 50, 10);
    p.setBrush(QColor(200,200,80)); p.drawRoundedRect(60, 34, 60, 12, 3, 3);

    p.end();

    // 2) Convertit en 1-bit (dither) → buffer SSD1306
    uint8_t fb[W*PAGES];
    qimage_to_ssd1306_1bit(canvas, fb);

    // 3) Envoie à l’écran
    int fd = open("/dev/i2c-1", O_RDWR);
    if(fd<0){ perror("open /dev/i2c-1"); return 1; }
    int addr = 0x3D;
    if(ioctl(fd, I2C_SLAVE, addr)<0){ perror("ioctl I2C_SLAVE"); return 1; }
    if(ssd1306_init(fd)!=0){ fprintf(stderr,"SSD1306 init fail\n"); return 1; }
    if(ssd1306_blit(fd, fb)!=0){ fprintf(stderr,"blit fail\n"); return 1; }
    close(fd);
    return 0;
}
