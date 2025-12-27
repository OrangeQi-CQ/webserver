cmake_minimum_required(VERSION 3.10)
project(WebServer)

# 设置C++标准（C++20）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置编译选项（优化+调试信息）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -pthread -Wall -Wextra")

# 包含头文件目录
include_directories(
    ${PROJECT_SOURCE_DIR}/include
)

# 收集源代码文件
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")

# 生成可执行文件
add_executable(server ${SOURCES})

# 链接线程库（如果需要）
target_link_libraries(server pthread)

execute_process(
    COMMAND chmod +x ${PROJECT_SOURCE_DIR}/test.sh
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

# 2. 添加自定义目标：test（可通过 make test 执行，依赖server编译完成）
add_custom_target(
    run_test  # 自定义目标名称（也可以直接叫test，注意避免和内置test目标冲突）
    DEPENDS server  # 依赖：先编译出server可执行文件，再执行测试
    COMMAND ${PROJECT_SOURCE_DIR}/test.sh  # 执行根目录的test.sh脚本
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}  # 工作目录：build目录（与编译产物同目录）
    COMMENT "正在执行WebServer ab并发测试..."  # 执行时的提示信息
)