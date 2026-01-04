CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra
LDFLAGS = -lm

TARGET = pca9685_servo
SOURCE = src/pca9685_servo.cpp

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean

