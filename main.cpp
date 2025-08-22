#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>

static constexpr int W = 128;
static constexpr int H = 64;
static constexpr int PAGES = H / 8;

// ---------- I2C / SSD1306 ----------
static int i2c_cmd(int fd, uint8_t c) {
    uint8_t b[2] = {0x00, c};
    return write(fd, b, 2) == 2 ? 0 : -1;
}
static int i2c_data(int fd, const uint8_t* d, size_t n) {
    // GROS CHUNKS pour réduire les syscalls (<=128 octets)
    uint8_t tmp[128 + 1]; tmp[0] = 0x40;
    size_t off = 0;
    while (off < n) {
        size_t k = std::min<size_t>(128, n - off);
        memcpy(tmp + 1, d + off, k);
        if (write(fd, tmp, k + 1) != (ssize_t)(k + 1)) return -1;
        off += k;
    }
    return 0;
}
static int ssd1306_init(int fd) {
    int r = 0;
    r |= i2c_cmd(fd, 0xAE);                 // display off
    r |= i2c_cmd(fd, 0x20); r |= i2c_cmd(fd, 0x00); // horizontal addressing
    r |= i2c_cmd(fd, 0xB0);
    r |= i2c_cmd(fd, 0xC8);                 // COM scan dec
    r |= i2c_cmd(fd, 0x00); r |= i2c_cmd(fd, 0x10); // column addr
    r |= i2c_cmd(fd, 0x40);                 // start line
    r |= i2c_cmd(fd, 0x81); r |= i2c_cmd(fd, 0x7F); // contrast
    r |= i2c_cmd(fd, 0xA1);                 // segment remap
    r |= i2c_cmd(fd, 0xA6);                 // normal
    r |= i2c_cmd(fd, 0xA8); r |= i2c_cmd(fd, 0x3F); // multiplex 1/64
    r |= i2c_cmd(fd, 0xA4);                 // display from RAM
    r |= i2c_cmd(fd, 0xD3); r |= i2c_cmd(fd, 0x00); // offset
    r |= i2c_cmd(fd, 0xD5); r |= i2c_cmd(fd, 0x80); // clock
    r |= i2c_cmd(fd, 0xD9); r |= i2c_cmd(fd, 0xF1); // pre-charge
    r |= i2c_cmd(fd, 0xDA); r |= i2c_cmd(fd, 0x12); // COM pins
    r |= i2c_cmd(fd, 0xDB); r |= i2c_cmd(fd, 0x40); // vcomh
    r |= i2c_cmd(fd, 0x8D); r |= i2c_cmd(fd, 0x14); // charge pump
    r |= i2c_cmd(fd, 0xAF);                 // display on
    return r;
}
static int ssd1306_blit(int fd, const uint8_t* fb) {
    // 1 write/page (grâce aux gros chunks)
    for (int p = 0; p < PAGES; ++p) {
        if (i2c_cmd(fd, 0xB0 + p)) return -1;
        if (i2c_cmd(fd, 0x00))     return -1;
        if (i2c_cmd(fd, 0x10))     return -1;
        if (i2c_data(fd, fb + p * W, W)) return -1;
    }
    return 0;
}

// ---------- Framebuffer & raster ultra léger ----------
struct FB {
    uint8_t buf[W * PAGES];
    inline void clear() { memset(buf, 0, sizeof(buf)); }
    inline void set(int x, int y) {
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
        buf[(y >> 3) * W + x] |= (1u << (y & 7));
    }
    // Bresenham int-only
    void line(int x0, int y0, int x1, int y1) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            set(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = err << 1;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
};

// ---------- 3D minimal (int math wherever possible) ----------
struct Vec3 { float x, y, z; };
static inline Vec3 rot(const Vec3& v, float ca, float sa, float cb, float sb) {
    // Rz * Ry (deux rotations, peu de trig dans la boucle)
    float x =  v.x * ca - v.y * sa;
    float y =  v.x * sa + v.y * ca;
    float z =  v.z;
    float x2 =  x * cb + z * sb;
    float z2 = -x * sb + z * cb;
    return {x2, y, z2};
}
static inline void proj(const Vec3& v, float fov, float zcam, int& ox, int& oy) {
    float z = v.z + zcam; if (z < 0.1f) z = 0.1f;
    float s = fov / z;
    ox = int(W * 0.5f + v.x * s + 0.5f);
    oy = int(H * 0.5f - v.y * s + 0.5f);
}

int main(int argc, char** argv) {
    // CONSEIL: mets dans /boot/config.txt : dtparam=i2c_arm=on,i2c_arm_baudrate=400000
    int addr = 0x3C;
    if (argc > 1) addr = int(strtol(argv[1], nullptr, 0)); // "0x3D" accepté

    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0) { perror("open /dev/i2c-1"); return 1; }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) { perror("ioctl I2C_SLAVE"); return 1; }
    if (ssd1306_init(fd) != 0) { fprintf(stderr, "SSD1306 init fail\n"); return 1; }

    // Cube
    const float s = 18.f;
    Vec3 V[8] = {
        {-s,-s,-s},{ s,-s,-s},{ s, s,-s},{-s, s,-s},
        {-s,-s, s},{ s,-s, s},{ s, s, s},{-s, s, s}
    };
    const uint8_t E[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };

    FB fb;
    float ang = 0.f;
    const float zcam = 60.f, fov = 92.f;

    // Boucle max-FPS (pas de sleep)
    for (;;) {
        fb.clear();

        // trig hors des boucles internes
        float ca = std::cos(ang), sa = std::sin(ang);
        float cb = std::cos(ang * 0.7f), sb = std::sin(ang * 0.7f);

        // proj
        int PX[8], PY[8];
        for (int i = 0; i < 8; ++i) {
            Vec3 r = rot(V[i], ca, sa, cb, sb);
            proj(r, fov, zcam, PX[i], PY[i]);
        }

        // edges (12 lignes)
        for (auto &e : E) {
            fb.line(PX[e[0]], PY[e[0]], PX[e[1]], PY[e[1]]);
        }

        // push
        if (ssd1306_blit(fd, fb.buf) != 0) { fprintf(stderr, "blit fail\n"); break; }

        // next frame
        ang += 0.06f; // ajuste si tu veux
        // NOTE: pas de sleep => FPS max. Si ça "tremble", mets ~5-10 ms.
        // std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    close(fd);
    return 0;
}
