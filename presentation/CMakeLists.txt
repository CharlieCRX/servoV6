# presentation/CMakeLists.txt

add_library(presentation
    viewmodel/AxisViewModelCore.cpp
    viewmodel/QtAxisViewModel.cpp
)

set_target_properties(presentation PROPERTIES AUTOMOC ON)

target_include_directories(presentation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(presentation
    PUBLIC
        Qt6::Core
        domain
        application
)