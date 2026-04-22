# presentation/CMakeLists.txt

add_library(presentation
    viewmodel/AxisViewModelCore.cpp
)

target_include_directories(presentation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(presentation
    PUBLIC
        domain
        application
)