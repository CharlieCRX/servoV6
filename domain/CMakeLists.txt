add_library(domain
    entity/Axis.h
    entity/Axis.cpp
    entity/AxisId.h
)

target_include_directories(domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
