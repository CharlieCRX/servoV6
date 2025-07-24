add_library(corelib
    BusinessLogic.cpp
    Motor.cpp
    IMotor.h
)

target_include_directories(corelib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
