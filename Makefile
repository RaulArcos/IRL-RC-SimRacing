CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra
LDFLAGS = -lm

all: pca9685_servo pca9685_motor

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGPIOD REQUIRED libgpiod)
target_include_directories(pca9685_motor PRIVATE ${LIBGPIOD_INCLUDE_DIRS})
target_link_libraries(pca9685_motor PRIVATE ${LIBGPIOD_LIBRARIES})

pca9685_servo: src/pca9685_servo.cpp
	$(CXX) $(CXXFLAGS) -o pca9685_servo src/pca9685_servo.cpp $(LDFLAGS)

pca9685_motor: src/pca9685_motor.cpp
	$(CXX) $(CXXFLAGS) -o pca9685_motor src/pca9685_motor.cpp -lgpiod $(LDFLAGS)

clean:
	rm -f pca9685_servo pca9685_motor

.PHONY: all clean
