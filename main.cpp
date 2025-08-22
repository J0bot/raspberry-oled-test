#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

static const int WIDTH = 128;
static const int HEIGHT = 64;
static const int PAGES = HEIGHT / 8;

int i2c_write_cmd(int fd, uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; // 0x00 = control byte (command)
    return write(fd, buf, 2) == 2 ? 0 : -1;
}
int i2c_write_data(int fd, const uint8_t* data, size_t len) {
    // 0x40 = control byte (data)
    uint8_t tmp[16+1];
    tmp[0] = 0x40;
    size_t off = 0;
    while (off < len) {
        size_t chunk = (len - off > 16) ? 16 : (len - off);
        memcpy(tmp + 1, data + off, chunk);
        if (write(fd, tmp, chunk + 1) != (ssize_t)(chunk + 1)) return -1;
        off += chunk;
    }
    return 0;
}
int ssd1306_init(int fd) {
    int r=0;
    r|=i2c_write_cmd(fd, 0xAE);           // display off
    r|=i2c_write_cmd(fd, 0x20); r|=i2c_write_cmd(fd, 0x00); // horiz addressing
    r|=i2c_write_cmd(fd, 0xB0);           // page start
    r|=i2c_write_cmd(fd, 0xC8);           // COM scan dec
    r|=i2c_write_cmd(fd, 0x00);           // low col
    r|=i2c_write_cmd(fd, 0x10);           // high col
    r|=i2c_write_cmd(fd, 0x40);           // start line
    r|=i2c_write_cmd(fd, 0x81); r|=i2c_write_cmd(fd, 0x7F); // contrast
    r|=i2c_write_cmd(fd, 0xA1);           // segment remap
    r|=i2c_write_cmd(fd, 0xA6);           // normal display
    r|=i2c_write_cmd(fd, 0xA8); r|=i2c_write_cmd(fd, 0x3F); // multiplex 1/64
    r|=i2c_write_cmd(fd, 0xA4);           // display from RAM
    r|=i2c_write_cmd(fd, 0xD3); r|=i2c_write_cmd(fd, 0x00); // display offset
    r|=i2c_write_cmd(fd, 0xD5); r|=i2c_write_cmd(fd, 0x80); // clock
    r|=i2c_write_cmd(fd, 0xD9); r|=i2c_write_cmd(fd, 0xF1); // pre-charge
    r|=i2c_write_cmd(fd, 0xDA); r|=i2c_write_cmd(fd, 0x12); // COM pins
    r|=i2c_write_cmd(fd, 0xDB); r|=i2c_write_cmd(fd, 0x40); // vcomh
    r|=i2c_write_cmd(fd, 0x8D); r|=i2c_write_cmd(fd, 0x14); // charge pump on
    r|=i2c_write_cmd(fd, 0xAF);           // display on
    return r;
}
int ssd1306_clear(int fd) {
    uint8_t line[WIDTH]; memset(line, 0x00, sizeof(line));
    for (int page = 0; page < PAGES; ++page) {
        i2c_write_cmd(fd, 0xB0 + page);
        i2c_write_cmd(fd, 0x00);
        i2c_write_cmd(fd, 0x10);
        if (i2c_write_data(fd, line, sizeof(line)) != 0) return -1;
    }
    return 0;
}
int ssd1306_blit(int fd, const uint8_t* fb) {
    for (int page = 0; page < PAGES; ++page) {
        i2c_write_cmd(fd, 0xB0 + page);
        i2c_write_cmd(fd, 0x00);
        i2c_write_cmd(fd, 0x10);
        if (i2c_write_data(fd, fb + page*WIDTH, WIDTH) != 0) return -1;
    }
    return 0;
}
int main() {
    const char* dev = "/dev/i2c-1";
    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open /dev/i2c-1"); return 1; }
    int addr = 0x3C;
    if (ioctl(fd, I2C_SLAVE, addr) < 0) { perror("ioctl I2C_SLAVE"); return 1; }
    if (ssd1306_init(fd) != 0) { fprintf(stderr, "init failed\n"); return 1; }
    ssd1306_clear(fd);

    // Framebuffer: WIDTH x PAGES (each byte = 8 vertical pixels)
    uint8_t fb[WIDTH * PAGES]; memset(fb, 0x00, sizeof(fb));

    // Animation: bar horizontale qui “scanne” l’écran
    for (int x = 0; x < WIDTH*3; ++x) {
        memset(fb, 0x00, sizeof(fb));
        int col = x % WIDTH;
        // dessiner une barre verticale de 8px de haut
        for (int y = 0; y < HEIGHT; ++y) {
            if (y/8 >= PAGES) continue;
            if (y >= 24 && y < 40) { // bande au milieu
                int page = y / 8;
                int bit = y % 8;
                fb[page*WIDTH + col] |= (1 << bit);
            }
        }
        ssd1306_blit(fd, fb);
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    close(fd);
    return 0;
}
