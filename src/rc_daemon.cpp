#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <chrono>

#include <gpiod.h>

constexpr uint8_t MODE1      = 0x00;
constexpr uint8_t MODE2      = 0x01;
constexpr uint8_t PRESCALE   = 0xFE;
constexpr uint8_t LED0_ON_L  = 0x06;

constexpr const char* I2C_DEV = "/dev/i2c-1";
constexpr uint8_t PCA_ADDR = 0x40;

constexpr uint8_t SERVO_CH = 0;
constexpr uint8_t MOTOR_CH = 4;

constexpr float SERVO_CENTER_US = 1800.0f;
constexpr float SERVO_LEFT_US   = 1400.0f;
constexpr float SERVO_RIGHT_US  = 2200.0f;

constexpr float MOTOR_MAX_DUTY = 0.85f;
constexpr int   DEADZONE_PERMILLE = 30;

constexpr int GPIO_STBY = 25;
constexpr int GPIO_AIN1 = 23;
constexpr int GPIO_AIN2 = 24;

constexpr uint16_t UDP_PORT = 6001;
constexpr int FAILSAFE_MS = 250;
constexpr const char* ALLOWED_PC_IP = "192.168.0.187"; 

static uint64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void i2cWrite(int fd, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    if (write(fd, buf, 2) != 2) throw std::runtime_error("I2C write failed");
}

static uint8_t i2cRead(int fd, uint8_t reg)
{
    if (write(fd, &reg, 1) != 1) throw std::runtime_error("I2C reg select failed");
    uint8_t v = 0;
    if (read(fd, &v, 1) != 1) throw std::runtime_error("I2C read failed");
    return v;
}

static void setPWMFreq(int fd, float freqHz)
{
    float prescaleVal = 25000000.0f / (4096.0f * freqHz) - 1.0f;
    uint8_t prescale = static_cast<uint8_t>(std::lround(prescaleVal));

    uint8_t oldMode = i2cRead(fd, MODE1);

    i2cWrite(fd, MODE1, (oldMode & 0x7F) | 0x10);
    i2cWrite(fd, PRESCALE, prescale);

    uint8_t wakeMode = (oldMode & ~0x10) | 0x20;
    i2cWrite(fd, MODE1, wakeMode);
    usleep(5000);

    i2cWrite(fd, MODE1, wakeMode | 0x80);
}

static void setPWM(int fd, uint8_t channel, uint16_t on, uint16_t off)
{
    uint8_t reg = LED0_ON_L + 4 * channel;
    i2cWrite(fd, reg + 0, on & 0xFF);
    i2cWrite(fd, reg + 1, on >> 8);
    i2cWrite(fd, reg + 2, off & 0xFF);
    i2cWrite(fd, reg + 3, off >> 8);
}

static void setDuty(int fd, uint8_t channel, float duty01)
{
    if (duty01 < 0.0f) duty01 = 0.0f;
    if (duty01 > 1.0f) duty01 = 1.0f;
    uint16_t off = static_cast<uint16_t>(std::lround(duty01 * 4095.0f));
    setPWM(fd, channel, 0, off);
}

static void setServoUS(int fd, uint8_t channel, float us)
{
    float ticksPerUS = 4096.0f / 20000.0f;
    uint16_t ticks = static_cast<uint16_t>(std::lround(us * ticksPerUS));
    if (ticks > 4095) ticks = 4095;
    setPWM(fd, channel, 0, ticks);
}

static float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

static float mapSteerPermilleToUs(int steerPermille)
{
    float s = clampf(steerPermille / 1000.0f, -1.0f, 1.0f);
    if (s < 0.0f) {
        return SERVO_CENTER_US + s * (SERVO_CENTER_US - SERVO_LEFT_US);
    }
    return SERVO_CENTER_US + s * (SERVO_RIGHT_US - SERVO_CENTER_US);
}

#pragma pack(push, 1)
struct Packet
{
    char magic[4];
    uint32_t seq;
    int16_t steer_pm;
    int16_t power_pm;
    uint16_t flags;
    uint16_t reserved;
};
#pragma pack(pop)

static bool parsePacket(const uint8_t* buf, size_t len, Packet& out)
{
    if (len != sizeof(Packet)) return false;         // exactly 16 bytes
    std::memcpy(&out, buf, sizeof(Packet));
    if (std::memcmp(out.magic, "IRL1", 4) != 0) return false;

    out.seq = ntohl(out.seq);

    uint16_t steer_u = ntohs(*reinterpret_cast<const uint16_t*>(&buf[8]));
    uint16_t power_u = ntohs(*reinterpret_cast<const uint16_t*>(&buf[10]));
    out.steer_pm = static_cast<int16_t>(steer_u);
    out.power_pm = static_cast<int16_t>(power_u);

    out.flags    = ntohs(*reinterpret_cast<const uint16_t*>(&buf[12]));
    out.reserved = ntohs(*reinterpret_cast<const uint16_t*>(&buf[14]));
    return true;
}

int main()
{
    try {
        gpiod_chip* chip = gpiod_chip_open("/dev/gpiochip0");
        if (!chip) throw std::runtime_error("Failed to open /dev/gpiochip0");

        gpiod_line_settings* ls = gpiod_line_settings_new();
        gpiod_line_config* lc = gpiod_line_config_new();
        gpiod_request_config* rc = gpiod_request_config_new();
        if (!ls || !lc || !rc) throw std::runtime_error("Failed to allocate gpiod configs");

        gpiod_line_settings_set_direction(ls, GPIOD_LINE_DIRECTION_OUTPUT);
        gpiod_line_settings_set_output_value(ls, GPIOD_LINE_VALUE_INACTIVE);

        unsigned int offsets[3] = { (unsigned)GPIO_STBY, (unsigned)GPIO_AIN1, (unsigned)GPIO_AIN2 };
        if (gpiod_line_config_add_line_settings(lc, offsets, 3, ls) < 0)
            throw std::runtime_error("gpiod_line_config_add_line_settings failed");

        gpiod_request_config_set_consumer(rc, "rc_car_daemon");
        gpiod_line_request* req = gpiod_chip_request_lines(chip, rc, lc);
        if (!req) throw std::runtime_error("gpiod_chip_request_lines failed");

        auto setSTBY = [&](bool on) {
            gpiod_line_request_set_value(req, GPIO_STBY, on ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
        };
        auto setDir = [&](bool forward) {
            gpiod_line_request_set_value(req, GPIO_AIN1, forward ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
            gpiod_line_request_set_value(req, GPIO_AIN2, forward ? GPIOD_LINE_VALUE_INACTIVE : GPIOD_LINE_VALUE_ACTIVE);
        };
        auto brake = [&]() {
            gpiod_line_request_set_value(req, GPIO_AIN1, GPIOD_LINE_VALUE_INACTIVE);
            gpiod_line_request_set_value(req, GPIO_AIN2, GPIOD_LINE_VALUE_INACTIVE);
        };

        setSTBY(false);
        brake();

        int i2cfd = open(I2C_DEV, O_RDWR);
        if (i2cfd < 0) { perror("open(i2c)"); return 1; }
        if (ioctl(i2cfd, I2C_SLAVE, PCA_ADDR) < 0) { perror("ioctl(I2C_SLAVE)"); close(i2cfd); return 1; }

        i2cWrite(i2cfd, MODE2, 0x04);
        i2cWrite(i2cfd, MODE1, 0x01 | 0x20);
        setPWMFreq(i2cfd, 50.0f);

        setDuty(i2cfd, MOTOR_CH, 0.0f);
        setServoUS(i2cfd, SERVO_CH, SERVO_CENTER_US);

        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) throw std::runtime_error("socket() failed");

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(UDP_PORT);

        if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            return 1;
        }

        printf("rc_car_daemon listening UDP :%u\n", UDP_PORT);

        uint64_t lastRxMs = 0;
        bool enabled = false;

        while (true) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 20 * 1000;

            int r = select(sock + 1, &rfds, nullptr, nullptr, &tv);
            if (r < 0) {
                if (errno == EINTR) continue;
                perror("select");
                break;
            }

            if (r > 0 && FD_ISSET(sock, &rfds)) {
                uint8_t buf[256];
                sockaddr_in src{};
                socklen_t srclen = sizeof(src);
                ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&src, &srclen);
                if (n > 0) {
                    char srcIp[INET_ADDRSTRLEN]{};
                    inet_ntop(AF_INET, &src.sin_addr, srcIp, sizeof(srcIp));
                    if (std::strcmp(srcIp, ALLOWED_PC_IP) != 0) {
                        continue; // ignore unknown senders
                    }
                    Packet p{};
                    if (parsePacket(buf, (size_t)n, p)) {
                        lastRxMs = nowMs();
                        enabled = (p.flags & 0x0001) != 0;
                        
                        printf("RX: seq=%u, steer=%d, power=%d, flags=0x%04x, enabled=%s\n",
                               p.seq, p.steer_pm, p.power_pm, p.flags, enabled ? "ON" : "OFF");

                        int steer = (int)p.steer_pm;
                        if (steer < -1000) steer = -1000;
                        if (steer >  1000) steer =  1000;
                        float us = mapSteerPermilleToUs(steer);
                        setServoUS(i2cfd, SERVO_CH, us);

                        int power = (int)p.power_pm;
                        if (power < -1000) power = -1000;
                        if (power >  1000) power =  1000;

                        if (!enabled) {
                            setDuty(i2cfd, MOTOR_CH, 0.0f);
                            brake();
                            setSTBY(false);
                        } else {
                            setSTBY(true);

                            if (std::abs(power) <= DEADZONE_PERMILLE) {
                                setDuty(i2cfd, MOTOR_CH, 0.0f);
                                brake();
                            } else {
                                bool forward = power > 0;
                                setDir(forward);

                                float duty = (std::abs(power) / 1000.0f) * MOTOR_MAX_DUTY;
                                setDuty(i2cfd, MOTOR_CH, duty);
                            }
                        }
                    }
                }
            }

            uint64_t now = nowMs();
            if (enabled && lastRxMs != 0 && (now - lastRxMs) > (uint64_t)FAILSAFE_MS) {
                enabled = false;
                setDuty(i2cfd, MOTOR_CH, 0.0f);
                setServoUS(i2cfd, SERVO_CH, SERVO_CENTER_US);
                brake();
                setSTBY(false);
                printf("FAILSAFE: no packets for %dms\n", FAILSAFE_MS);
            }
        }

        setDuty(i2cfd, MOTOR_CH, 0.0f);
        setServoUS(i2cfd, SERVO_CH, SERVO_CENTER_US);
        setSTBY(false);

        close(sock);
        close(i2cfd);

        gpiod_line_request_release(req);
        gpiod_request_config_free(rc);
        gpiod_line_config_free(lc);
        gpiod_line_settings_free(ls);
        gpiod_chip_close(chip);
        return 0;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
