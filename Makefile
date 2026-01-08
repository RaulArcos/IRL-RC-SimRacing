CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra
LDFLAGS = -lm

all: pca9685_servo pca9685_motor

pca9685_servo: src/pca9685_servo.cpp
	$(CXX) $(CXXFLAGS) -o pca9685_servo src/pca9685_servo.cpp $(LDFLAGS)

pca9685_motor: src/pca9685_motor.cpp
	$(CXX) $(CXXFLAGS) -o pca9685_motor src/pca9685_motor.cpp -lgpiod $(LDFLAGS)

clean:
	rm -f pca9685_servo pca9685_motor

.PHONY: all clean
