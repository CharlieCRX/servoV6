# F:/project/servoV6/application/CMakeLists.txt

add_library(application_lib STATIC
    BusinessLogic.cpp
    MotorCommandExecutor.cpp
    # 如果还有其他属于 application 模块的 .cpp 文件，也在这里列出
)

# 设置公共包含目录，以便其他模块可以 #include "BusinessLogic.h" 等
target_include_directories(application_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 应用服务层会依赖 domain 层的接口 (IMotor, IServoAdapter, MovementCommand, CommandVisitor)
# 它可能还会依赖 utils_lib 用于日志，以及 infrastructure_transport_lib 如果它直接与 ModbusClient 交互。
target_link_libraries(application_lib PUBLIC
    domain_lib                   # 获取所有接口定义
    utilslib                     # Logger 等
    infrastructure_transport_lib # 如果 BusinessLogic 或 MotorCommandExecutor 直接调用 ModbusClient
    # 注意：如果 BusinessLogic 或 MotorCommandExecutor 还需要使用到 adapters 层（例如 LinearServoAdapter, Motor）
    # 的具体实现，并且 adapters 模块还没有迁移，那么这里暂时不能链接。
    # 如果 adapters 已经迁移了，则在这里链接：
    # adapters_servos_lib
    # adapters_motors_lib
)