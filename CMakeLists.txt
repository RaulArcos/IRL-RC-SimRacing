cmake_minimum_required(VERSION 3.10)
project(IRL-RC-SimRacing)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable all warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_executable(pca9685_servo src/pca9685_servo.cpp)

# Link against math library (required for std::round)
target_link_libraries(pca9685_servo m)

