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
constexpr uint8_t PRESCALE   = 0xFE;
constexpr uint8_t LED0_ON_L  = 0x06;

int i2cWrite(int fd, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    int result = write(fd, buf, 2);
    if (result != 2) {
        throw std::runtime_error("I2C write failed");
    }
    return result;
}

uint8_t i2cRead(int fd, uint8_t reg)
{
    if (write(fd, &reg, 1) != 1) {
        throw std::runtime_error("I2C register write failed");
    }
    uint8_t value;
    if (read(fd, &value, 1) != 1) {
        throw std::runtime_error("I2C read failed");
    }
    return value;
}

void setPWMFreq(int fd, float freqHz)
{
    float prescaleVal = 25000000.0f / (4096.0f * freqHz) - 1.0f;
    uint8_t prescale = static_cast<uint8_t>(std::round(prescaleVal));

    uint8_t oldMode = i2cRead(fd, MODE1);
    i2cWrite(fd, MODE1, (oldMode & 0x7F) | 0x10); // sleep
    i2cWrite(fd, PRESCALE, prescale);
    i2cWrite(fd, MODE1, oldMode);
    usleep(5000);
    i2cWrite(fd, MODE1, oldMode | 0x80); // restart
}

void setPWM(int fd, uint8_t channel, uint16_t on, uint16_t off)
{
    uint8_t reg = LED0_ON_L + 4 * channel;
    i2cWrite(fd, reg + 0, on & 0xFF);
    i2cWrite(fd, reg + 1, on >> 8);
    i2cWrite(fd, reg + 2, off & 0xFF);
    i2cWrite(fd, reg + 3, off >> 8);
}

void setServoUS(int fd, uint8_t channel, float us)
{
    // 50 Hz → 20 ms period
    float ticksPerUS = 4096.0f / 20000.0f;
    uint16_t ticks = static_cast<uint16_t>(us * ticksPerUS);
    setPWM(fd, channel, 0, ticks);
}

int main()
{
    const char* device = "/dev/i2c-1";
    const uint8_t PCA_ADDR = 0x40;
    const uint8_t CHANNEL = 0; // change if needed

    try {
        int fd = open(device, O_RDWR);
        if (fd < 0) {
            perror("Failed to open I2C device");
            return 1;
        }

        if (ioctl(fd, I2C_SLAVE, PCA_ADDR) < 0) {
            perror("Failed to select PCA9685");
            close(fd);
            return 1;
        }

        setPWMFreq(fd, 50.0f);

        printf("Center\n");
        setServoUS(fd, CHANNEL, 1500);
        sleep(2);

        printf("Left\n");
        setServoUS(fd, CHANNEL, 1100);
        sleep(2);

        printf("Right\n");
        setServoUS(fd, CHANNEL, 1900);
        sleep(2);

        printf("Back to center\n");
        setServoUS(fd, CHANNEL, 1500);

        close(fd);
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}

