add_library(corelib
    MovementCommand.cpp
    BusinessLogic.cpp
    Motor.cpp
)

target_include_directories(corelib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
