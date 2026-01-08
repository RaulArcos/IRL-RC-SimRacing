CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra
CXXFLAGS_DAEMON = -std=c++17 -Wall -Wextra
LDFLAGS = -lm

all: pca9685_servo pca9685_motor rc_daemon

pca9685_servo: src/pca9685_servo.cpp
	$(CXX) $(CXXFLAGS) -o pca9685_servo src/pca9685_servo.cpp $(LDFLAGS)

pca9685_motor: src/pca9685_motor.cpp
	$(CXX) $(CXXFLAGS) -o pca9685_motor src/pca9685_motor.cpp -lgpiod $(LDFLAGS)

rc_daemon: src/rc_daemon.cpp
	$(CXX) $(CXXFLAGS_DAEMON) -o rc_daemon src/rc_daemon.cpp -lgpiod $(LDFLAGS)

clean:
	rm -f pca9685_servo pca9685_motor rc_daemon

.PHONY: all clean
