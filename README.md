# IRL-RC-SimRacing

Real-life RC Sim Racing project with PCA9685 servo control.

## Building on Raspberry Pi 2W

### Prerequisites
```bash
sudo apt-get update
sudo apt-get install build-essential cmake
```

### Using CMake
```bash
mkdir build
cd build
cmake ..
make
```

### Using Make
```bash
make
```

The executable will be created as `pca9685_servo` (or `build/pca9685_servo` if using CMake).

## Running

**Note:** Requires I2C to be enabled and root/sudo access.

1. Enable I2C on Raspberry Pi:
   ```bash
   sudo raspi-config
   # Navigate to Interface Options → I2C → Enable
   ```

2. Check I2C device exists:
   ```bash
   ls -l /dev/i2c-*
   ```

3. Run the program:
   ```bash
   sudo ./pca9685_servo
   # or if built with CMake:
   sudo ./build/pca9685_servo
   ```

## Hardware Setup

- PCA9685 PWM/Servo Driver connected via I2C
- Default I2C device: `/dev/i2c-1`
- Default PCA9685 address: `0x40`
- Default servo channel: `0`

Modify these constants in `src/pca9685_servo.cpp` if your setup differs.