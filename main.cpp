#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

// Taille écran
static const int WIDTH = 128;
static const int HEIGHT = 64;
static const int PAGES = HEIGHT / 8;

// Police 5x7 ASCII pour "HELLO WORLD"
static const uint8_t FONT5x7[96][5] = {
{0,0,0,0,0}, // ' '
{0,0,95,0,0}, // !
{0,7,0,7,0}, // "
{20,127,20,127,20}, // #
{36,42,127,42,18}, // $
{35,19,8,100,98}, // %
{54,73,85,34,80}, // &
{0,5,3,0,0}, // '
{0,28,34,65,0}, // (
{0,65,34,28,0}, // )
{20,8,62,8,20}, // *
{8,8,62,8,8}, // +
{0,80,48,0,0}, // ,
{8,8,8,8,8}, // -
{0,96,96,0,0}, // .
{32,16,8,4,2}, // /
{62,81,73,69,62}, // 0
{0,66,127,64,0}, // 1
{66,97,81,73,70}, // 2
{33,65,73,77,51}, // 3
{24,20,18,127,16}, // 4
{39,69,69,69,57}, // 5
{60,74,73,73,48}, // 6
{1,113,9,5,3}, // 7
{54,73,73,73,54}, // 8
{6,73,73,41,30}, // 9
{0,54,54,0,0}, // :
{0,86,54,0,0}, // ;
{8,20,34,65,0}, // <
{20,20,20,20,20}, // =
{0,65,34,20,8}, // >
{2,1,81,9,6}, // ?
{50,73,121,65,62}, // @
{126,17,17,17,126}, // A
{127,73,73,73,54}, // B
{62,65,65,65,34}, // C
{127,65,65,34,28}, // D
{127,73,73,73,65}, // E
{127,9,9,9,1}, // F
{62,65,73,73,122}, // G
{127,8,8,8,127}, // H
{0,65,127,65,0}, // I
{32,64,65,63,1}, // J
{127,8,20,34,65}, // K
{127,64,64,64,64}, // L
{127,2,12,2,127}, // M
{127,4,8,16,127}, // N
{62,65,65,65,62}, // O
{127,9,9,9,6}, // P
{62,65,81,33,94}, // Q
{127,9,25,41,70}, // R
{38,73,73,73,50}, // S
{1,1,127,1,1}, // T
{63,64,64,64,63}, // U
{31,32,64,32,31}, // V
{127,32,24,32,127}, // W
{99,20,8,20,99}, // X
{7,8,112,8,7}, // Y
{97,81,73,69,67}, // Z
};

int i2c_write_cmd(int fd, uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    return write(fd, buf, 2);
}
int i2c_write_data(int fd, const uint8_t* data, size_t len) {
    uint8_t tmp[16+1]; tmp[0] = 0x40;
    size_t off = 0;
    while (off < len) {
        size_t chunk = (len - off > 16) ? 16 : (len - off);
        memcpy(tmp+1, data+off, chunk);
        if (write(fd, tmp, chunk+1) != (ssize_t)(chunk+1)) return -1;
        off += chunk;
    }
    return 0;
}
int ssd1306_init(int fd) {
    i2c_write_cmd(fd, 0xAE);
    i2c_write_cmd(fd, 0x20); i2c_write_cmd(fd, 0x00);
    i2c_write_cmd(fd, 0xB0);
    i2c_write_cmd(fd, 0xC8);
    i2c_write_cmd(fd, 0x00); i2c_write_cmd(fd, 0x10);
    i2c_write_cmd(fd, 0x40);
    i2c_write_cmd(fd, 0x81); i2c_write_cmd(fd, 0x7F);
    i2c_write_cmd(fd, 0xA1);
    i2c_write_cmd(fd, 0xA6);
    i2c_write_cmd(fd, 0xA8); i2c_write_cmd(fd, 0x3F);
    i2c_write_cmd(fd, 0xA4);
    i2c_write_cmd(fd, 0xD3); i2c_write_cmd(fd, 0x00);
    i2c_write_cmd(fd, 0xD5); i2c_write_cmd(fd, 0x80);
    i2c_write_cmd(fd, 0xD9); i2c_write_cmd(fd, 0xF1);
    i2c_write_cmd(fd, 0xDA); i2c_write_cmd(fd, 0x12);
    i2c_write_cmd(fd, 0xDB); i2c_write_cmd(fd, 0x40);
    i2c_write_cmd(fd, 0x8D); i2c_write_cmd(fd, 0x14);
    i2c_write_cmd(fd, 0xAF);
    return 0;
}

void drawChar(uint8_t *fb, int x, int y, char c) {
    if (c < 32 || c > 127) return;
    const uint8_t *glyph = FONT5x7[c-32];
    for (int i=0; i<5; i++) {
        for (int bit=0; bit<7; bit++) {
            if (glyph[i] & (1<<bit)) {
                int yy = y+bit;
                int page = yy/8;
                int bitpos = yy%8;
                fb[page*WIDTH + (x+i)] |= (1<<bitpos);
            }
        }
    }
}

int main() {
    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    if (ioctl(fd, I2C_SLAVE, 0x3D) < 0) { perror("ioctl"); return 1; }

    ssd1306_init(fd);

    // framebuffer vide
    uint8_t fb[WIDTH*PAGES]; memset(fb,0,sizeof(fb));

    // écrire "HELLO WORLD" au centre
    std::string msg="HELLO WORLD";
    int startx=(WIDTH - msg.size()*6)/2;
    int starty=(HEIGHT-7)/2;
    for (size_t i=0;i<msg.size();i++)
        drawChar(fb,startx+i*6,starty,msg[i]);

    // push à l’écran
    for (int page=0; page<PAGES; page++) {
        i2c_write_cmd(fd,0xB0+page);
        i2c_write_cmd(fd,0x00);
        i2c_write_cmd(fd,0x10);
        i2c_write_data(fd, fb+page*WIDTH, WIDTH);
    }

    close(fd);
    return 0;
}
