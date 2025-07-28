# F:/project/servoV6/infrastructure/CMakeLists.txt

add_subdirectory(transport)

# 你也可以选择定义一个顶层的接口库来聚合所有基础设施组件的 PUBLIC 依赖
# 例如：
# add_library(infrastructure_lib INTERFACE)
# target_link_libraries(infrastructure_lib INTERFACE infrastructure_transport_lib)