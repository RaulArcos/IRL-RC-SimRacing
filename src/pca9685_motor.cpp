#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <stdexcept>

#include <gpiod.h>

// ===================== PCA9685 =====================
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

    i2cWrite(fd, MODE1, (oldMode & 0x7F) | 0x10); // sleep
    i2cWrite(fd, PRESCALE, prescale);

    uint8_t wakeMode = (oldMode & ~0x10) | 0x20; // AI
    i2cWrite(fd, MODE1, wakeMode);
    usleep(5000);
    i2cWrite(fd, MODE1, wakeMode | 0x80); // restart
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

int main()
{
    // ====== EDIT THESE if your wiring differs (BCM GPIO numbers) ======
    constexpr int GPIO_STBY = 23;
    constexpr int GPIO_AIN1 = 24;
    constexpr int GPIO_AIN2 = 25;

    constexpr uint8_t MOTOR_CH = 4;
    const char* device = "/dev/i2c-1";
    const uint8_t PCA_ADDR = 0x40;

    try {
        // ---- GPIO (libgpiod) ----
        gpiod_chip* chip = gpiod_chip_open_by_name("gpiochip0");
        if (!chip) throw std::runtime_error("Failed to open gpiochip0");

        gpiod_line* stby = gpiod_chip_get_line(chip, GPIO_STBY);
        gpiod_line* ain1 = gpiod_chip_get_line(chip, GPIO_AIN1);
        gpiod_line* ain2 = gpiod_chip_get_line(chip, GPIO_AIN2);
        if (!stby || !ain1 || !ain2) throw std::runtime_error("Failed to get GPIO line(s)");

        if (gpiod_line_request_output(stby, "pca9685_motor", 0) < 0) throw std::runtime_error("STBY request failed");
        if (gpiod_line_request_output(ain1, "pca9685_motor", 0) < 0) throw std::runtime_error("AIN1 request failed");
        if (gpiod_line_request_output(ain2, "pca9685_motor", 0) < 0) throw std::runtime_error("AIN2 request failed");

        // Enable TB6612 + forward
        gpiod_line_set_value(stby, 1);
        gpiod_line_set_value(ain1, 1);
        gpiod_line_set_value(ain2, 0);

        // ---- PCA9685 ----
        int fd = open(device, O_RDWR);
        if (fd < 0) { perror("open"); return 1; }
        if (ioctl(fd, I2C_SLAVE, PCA_ADDR) < 0) { perror("ioctl(I2C_SLAVE)"); close(fd); return 1; }

        i2cWrite(fd, MODE2, 0x04);         // OUTDRV
        i2cWrite(fd, MODE1, 0x01 | 0x20);  // ALLCALL + AI
        setPWMFreq(fd, 1000.0f);          // 1 kHz for motor PWM

        printf("Ramping motor on PCA channel %u...\n", MOTOR_CH);

        setDuty(fd, MOTOR_CH, 0.0f);
        usleep(200000);

        // ramp up to 30%
        for (int i = 0; i <= 60; i++) {
            float duty = (0.30f * i) / 60.0f;
            setDuty(fd, MOTOR_CH, duty);
            usleep(40000);
        }

        printf("Hold...\n");
        sleep(2);

        // ramp down
        for (int i = 60; i >= 0; i--) {
            float duty = (0.30f * i) / 60.0f;
            setDuty(fd, MOTOR_CH, duty);
            usleep(40000);
        }

        setDuty(fd, MOTOR_CH, 0.0f);

        // disable standby
        gpiod_line_set_value(stby, 0);

        close(fd);

        // release GPIO
        gpiod_line_release(stby);
        gpiod_line_release(ain1);
        gpiod_line_release(ain2);
        gpiod_chip_close(chip);

        printf("Done.\n");
        return 0;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
