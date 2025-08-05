# tests/CMakeLists.txt

# --- Qt 相关设置 ---
# 自动处理 Qt 元对象编译器 (MOC)
# MOC 负责处理 Qt 的 Q_OBJECT 宏，为信号、槽和属性等机制生成必要的 C++ 代码。
# 启用后，CMake 会自动扫描源文件和头文件，查找 Q_OBJECT 宏并运行 MOC。
set(CMAKE_AUTOMOC ON)

# 自动处理 Qt 用户界面编译器 (UIC)
# UIC 负责将 Qt Creator 生成的 .ui 文件（用户界面描述文件）转换为 C++ 代码。
# 你的测试项目可能不直接使用 .ui 文件，但保留此设置以备将来扩展。
set(CMAKE_AUTORCC ON)

# --- 定义测试源文件 ---
# TEST_SOURCES 变量列出了构成你的测试可执行文件的所有源文件。
# 当你添加新的测试文件时，需要将它们添加到这个列表中。
# 1. test_main.cpp: 包含 main() 函数，它是程序的入口点，通常负责初始化测试框架。
# 2. test_*.cpp: 包含测试类（例如 SerialCommProtocolTest）的实现。
# 3. test_*.h: 包含带有 Q_OBJECT 宏的测试类定义。MOC 需要扫描这些头文件来生成 moc_*.cpp 文件。
set(TEST_SOURCES
    test_main.cpp
    test_serialcommprotocol.h test_serialcommprotocol.cpp # <-- 如果有新的测试类头文件（带Q_OBJECT），也要添加到这里
    test_p100smotor.h test_p100smotor.cpp
    test_motorregisteraccessor.h test_motorregisteraccessor.cpp
    test_gear_rotary_adapter.h test_gear_rotary_adapter.cpp
)

# --- 创建测试可执行文件 ---
# `add_executable()` 命令创建一个可执行文件目标。
# my_test_runner 是最终生成的可执行文件的名称。
add_executable(my_test_runner ${TEST_SOURCES})

# --- MOC 优化设置 ---
# 避免对 .cpp 文件进行不必要的 MOC 扫描。
# 因为 Q_OBJECT 宏通常定义在其对应的头文件 (.h) 中，而不是 .cpp 文件本身。
# 明确指定 SKIP_AUTOMOC ON 可以提高构建速度，防止 MOC 扫描重复工作。
set_property(SOURCE test_serialcommprotocol.cpp PROPERTY SKIP_AUTOMOC ON)
# 当你添加新的测试类（例如 test_new_feature.h 和 test_new_feature.cpp）时：
# - 如果 test_new_feature.h 中有 Q_OBJECT 宏，确保它在 TEST_SOURCES 列表中。
# - 如果 test_new_feature.cpp 中没有 Q_OBJECT 宏，但包含了 test_new_feature.h，
#   那么你应该为 test_new_feature.cpp 也添加 SKIP_AUTOMOC ON 属性。
# set_property(SOURCE test_new_feature.cpp PROPERTY SKIP_AUTOMOC ON)


# --- 链接所需库 ---
# `target_link_libraries()` 命令将测试可执行文件与它所依赖的库进行链接。
# PRIVATE 关键字表示这些库只供此目标使用，不会传递给依赖此目标的父目标。
target_link_libraries(my_test_runner
    PRIVATE
        Qt6::Core             # Qt 核心模块，提供基本非 GUI 功能
        Qt6::Test             # Qt Test 框架库，用于编写单元测试
        application_lib       # 你的应用程序核心逻辑库（例如，可能包含业务逻辑）
        adapters_protocol_lib # 你的协议适配器库
        adapters_motors_lib   # 电机型号适配库
        adapters_servos_lib   # 功能适配库
        utils_lib             # 你的工具类库（例如，Logger 所在库）
        # 以下是根据你的项目需要可能需要取消注释的库，或者添加新的库
        # spdlogd             # 如果你的 Logger 库依赖于 spdlog，请取消注释。'd' 通常表示调试版本。
        # Qt6::SerialPort     # 如果你的 SerialCommProtocol 使用 QtSerialPort 库，请取消注释。
        # Qt6::SerialBus      # 如果涉及串口总线通信，请取消注释。
        # Qt6::Network        # 如果测试中涉及网络通信，请取消注释。
)

# --- 添加头文件搜索路径 ---
# `target_include_directories()` 命令告诉编译器在哪里查找 #include 语句中的头文件。
# PRIVATE 关键字表示这些路径只供此目标使用。
target_include_directories(my_test_runner
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}        # 当前 tests 目录，包含 test_main.cpp 和测试类的头文件
        # 当你添加新的模块或库时，如果其头文件不在上述路径中，你需要在这里添加新的路径。
        # 例如：
        # ${CMAKE_SOURCE_DIR}/application/include # 如果 application_lib 的公共头文件在这里
        # ${CMAKE_SOURCE_DIR}/domain/include      # 如果 domain_lib 的公共头文件在这里
)

# --- CTest 集成 ---
# `add_test()` 命令将你的可执行文件注册为 CTest 的一个测试项。
# 这允许你使用 `ctest` 命令来运行你的测试，例如在命令行中执行 `ctest`。
add_test(NAME run_all_tests COMMAND my_test_runner)

# --- 可选：在每次构建时自动运行测试 ---
# `add_custom_target()` 创建一个自定义目标。
# ALL 关键字表示这个目标会在默认的构建过程中执行。
# COMMAND 后面是你希望执行的命令，这里是 CTest 命令，它会运行所有测试并在失败时输出详细信息。
# 启用此行后，每次你在 Qt Creator 中点击“构建”时，测试也会自动运行。
# add_custom_target(run_all_tests_on_build ALL COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure)
