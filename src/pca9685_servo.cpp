#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <stdexcept>

// PCA9685 registers
constexpr uint8_t MODE1      = 0x00;
constexpr uint8_t MODE2      = 0x01;
constexpr uint8_t PRESCALE   = 0xFE;
constexpr uint8_t LED0_ON_L  = 0x06;

static void i2cWrite(int fd, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    if (write(fd, buf, 2) != 2) {
        throw std::runtime_error("I2C write failed");
    }
}

static uint8_t i2cRead(int fd, uint8_t reg)
{
    if (write(fd, &reg, 1) != 1) {
        throw std::runtime_error("I2C register select failed");
    }
    uint8_t value = 0;
    if (read(fd, &value, 1) != 1) {
        throw std::runtime_error("I2C read failed");
    }
    return value;
}

static void dumpRegs(int fd, const char* tag)
{
    uint8_t m1 = i2cRead(fd, MODE1);
    uint8_t m2 = i2cRead(fd, MODE2);
    uint8_t ps = i2cRead(fd, PRESCALE);
    std::printf("[%s] MODE1=0x%02X MODE2=0x%02X PRESCALE=0x%02X\n", tag, m1, m2, ps);
}

static void setPWMFreq(int fd, float freqHz)
{
    // prescale = round(25MHz / (4096*freq) - 1)
    float prescaleVal = 25000000.0f / (4096.0f * freqHz) - 1.0f;
    uint8_t prescale = static_cast<uint8_t>(std::lround(prescaleVal));

    uint8_t oldMode = i2cRead(fd, MODE1);

    // Sleep
    i2cWrite(fd, MODE1, (oldMode & 0x7F) | 0x10);
    // Set prescale
    i2cWrite(fd, PRESCALE, prescale);
    // Wake + AI
    uint8_t wakeMode = (oldMode & ~0x10) | 0x20; // clear SLEEP, set AI
    i2cWrite(fd, MODE1, wakeMode);
    usleep(5000);
    // Restart (optional, but commonly used)
    i2cWrite(fd, MODE1, wakeMode | 0x80);
}

static void setPWM(int fd, uint8_t channel, uint16_t on, uint16_t off)
{
    if (channel > 15) throw std::runtime_error("Invalid channel");
    if (on >= 4096) on = 4095;
    if (off >= 4096) off = 4095;

    uint8_t reg = LED0_ON_L + 4 * channel;
    i2cWrite(fd, reg + 0, on & 0xFF);
    i2cWrite(fd, reg + 1, on >> 8);
    i2cWrite(fd, reg + 2, off & 0xFF);
    i2cWrite(fd, reg + 3, off >> 8);
}

static void setServoUS(int fd, uint8_t channel, float us)
{
    // 50 Hz => 20,000 us period
    // ticks = us * 4096 / 20000
    float ticksPerUS = 4096.0f / 20000.0f;
    uint16_t ticks = static_cast<uint16_t>(std::lround(us * ticksPerUS));
    if (ticks > 4095) ticks = 4095;
    setPWM(fd, channel, 0, ticks);
}

int main()
{
    const char* device = "/dev/i2c-1";
    const uint8_t PCA_ADDR = 0x40;
    const uint8_t CHANNEL = 0;

    try {
        int fd = open(device, O_RDWR);
        if (fd < 0) { perror("open"); return 1; }

        if (ioctl(fd, I2C_SLAVE, PCA_ADDR) < 0) {
            perror("ioctl(I2C_SLAVE)");
            close(fd);
            return 1;
        }

        dumpRegs(fd, "BEFORE");

        // Typical servo-friendly output config
        i2cWrite(fd, MODE2, 0x04); // OUTDRV
        // MODE1: ALLCALL + AI (we'll also set AI in setPWMFreq)
        i2cWrite(fd, MODE1, 0x01 | 0x20);

        setPWMFreq(fd, 50.0f);

        dumpRegs(fd, "AFTER");

        std::printf("Center\n");
        setServoUS(fd, CHANNEL, 1500); sleep(2);

        std::printf("Left\n");
        setServoUS(fd, CHANNEL, 1100); sleep(2);

        std::printf("Right\n");
        setServoUS(fd, CHANNEL, 1900); sleep(2);

        std::printf("Back to center\n");
        setServoUS(fd, CHANNEL, 1500);

        close(fd);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
