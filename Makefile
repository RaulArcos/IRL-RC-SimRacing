CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra
CXXFLAGS_DAEMON = -std=c++17 -Wall -Wextra

LDFLAGS = -lm
GPIO_LIBS = -lgpiod

GST_CFLAGS = $(shell pkg-config --cflags gstreamer-1.0 gstreamer-base-1.0 glib-2.0)
GST_LIBS   = $(shell pkg-config --libs   gstreamer-1.0 gstreamer-base-1.0 glib-2.0)

all: pca9685_servo pca9685_motor rc_daemon video_sender

pca9685_servo: src/pca9685_servo.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

pca9685_motor: src/pca9685_motor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(GPIO_LIBS) $(LDFLAGS)

rc_daemon: src/rc_daemon.cpp
	$(CXX) $(CXXFLAGS_DAEMON) -o $@ $< $(GPIO_LIBS) $(LDFLAGS)

video_sender: src/video_sender.cpp
	$(CXX) $(CXXFLAGS_DAEMON) $(GST_CFLAGS) -o $@ $< $(GST_LIBS)

clean:
	rm -f pca9685_servo pca9685_motor rc_daemon video_sender

.PHONY: all clean
