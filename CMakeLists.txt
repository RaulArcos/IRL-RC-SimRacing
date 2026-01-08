cmake_minimum_required(VERSION 3.10)
project(IRL-RC-SimRacing)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable all warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_executable(pca9685_servo src/pca9685_servo.cpp)
add_executable(pca9685_motor src/pca9685_motor.cpp)
add_executable(rc_daemon src/rc_daemon.cpp)

# Link against math library (required for std::round)
target_link_libraries(pca9685_servo m)

# Link against math library and libgpiod (for GPIO control)
target_link_libraries(pca9685_motor m gpiod)

# rc_daemon requires C++17 and libgpiod
set_target_properties(rc_daemon PROPERTIES CXX_STANDARD 17)
target_link_libraries(rc_daemon m gpiod)
