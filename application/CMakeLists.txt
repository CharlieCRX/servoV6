# F:/project/servoV6/application/CMakeLists.txt

add_library(application_lib STATIC
    BusinessLogic.cpp
    MotorCommandExecutor.cpp
    # 如果还有其他属于 application 模块的 .cpp 文件，也在这里列出
)

# 设置公共包含目录，以便其他模块可以 #include "BusinessLogic.h" 等
target_include_directories(application_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 应用服务层会依赖 domain 层的接口 (IMotor, IServoAdapter, MovementCommand, CommandVisitor)
target_link_libraries(application_lib PUBLIC
    domain_lib                   # 获取所有接口定义
    utilslib                     # Logger 等
)