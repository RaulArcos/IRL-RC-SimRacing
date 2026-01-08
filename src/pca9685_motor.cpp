#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>

constexpr uint8_t MODE1      = 0x00;
constexpr uint8_t MODE2      = 0x01;
constexpr uint8_t PRESCALE   = 0xFE;
constexpr uint8_t LED0_ON_L  = 0x06;

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
    if (channel > 15) throw std::runtime_error("Invalid PCA channel");
    if (on >= 4096) on = 4095;
    if (off >= 4096) off = 4095;

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

static void writeFile(const std::string& path, const std::string& value)
{
    int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) throw std::runtime_error("Failed to open: " + path);
    if (write(fd, value.c_str(), value.size()) < 0) {
        close(fd);
        throw std::runtime_error("Failed to write: " + path);
    }
    close(fd);
}

static bool existsPath(const std::string& path)
{
    return access(path.c_str(), F_OK) == 0;
}

static void gpioExport(int gpio)
{
    if (!existsPath("/sys/class/gpio/gpio" + std::to_string(gpio)))
        writeFile("/sys/class/gpio/export", std::to_string(gpio));
}

static void gpioUnexport(int gpio)
{
    if (existsPath("/sys/class/gpio/gpio" + std::to_string(gpio)))
        writeFile("/sys/class/gpio/unexport", std::to_string(gpio));
}

static void gpioDirOut(int gpio)
{
    writeFile("/sys/class/gpio/gpio" + std::to_string(gpio) + "/direction", "out");
}

static void gpioWrite(int gpio, int value)
{
    writeFile("/sys/class/gpio/gpio" + std::to_string(gpio) + "/value", value ? "1" : "0");
}

int main()
{
    constexpr int GPIO_STBY = 23;
    constexpr int GPIO_AIN1 = 24;
    constexpr int GPIO_AIN2 = 25;

    constexpr uint8_t MOTOR_CH = 4;

    const char* device = "/dev/i2c-1";
    const uint8_t PCA_ADDR = 0x40;

    try {
        gpioExport(GPIO_STBY);
        gpioExport(GPIO_AIN1);
        gpioExport(GPIO_AIN2);
        gpioDirOut(GPIO_STBY);
        gpioDirOut(GPIO_AIN1);
        gpioDirOut(GPIO_AIN2);

        gpioWrite(GPIO_STBY, 1);
        gpioWrite(GPIO_AIN1, 1);
        gpioWrite(GPIO_AIN2, 0);

        int fd = open(device, O_RDWR);
        if (fd < 0) { perror("open"); return 1; }
        if (ioctl(fd, I2C_SLAVE, PCA_ADDR) < 0) { perror("ioctl(I2C_SLAVE)"); close(fd); return 1; }

        i2cWrite(fd, MODE2, 0x04);
        i2cWrite(fd, MODE1, 0x01 | 0x20);
        setPWMFreq(fd, 1000.0f);

        std::printf("Ramping motor on PCA channel %u...\n", MOTOR_CH);

        setDuty(fd, MOTOR_CH, 0.0f);
        usleep(200000);

        for (int i = 0; i <= 60; i++) {
            float duty = (0.30f * i) / 60.0f;
            setDuty(fd, MOTOR_CH, duty);
            usleep(40000);
        }

        std::printf("Hold...\n");
        sleep(2);

        for (int i = 60; i >= 0; i--) {
            float duty = (0.30f * i) / 60.0f;
            setDuty(fd, MOTOR_CH, duty);
            usleep(40000);
        }

        setDuty(fd, MOTOR_CH, 0.0f);
        gpioWrite(GPIO_STBY, 0);

        close(fd);

        std::printf("Done.\n");
        return 0;
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
