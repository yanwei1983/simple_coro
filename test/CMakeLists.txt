cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

PROJECT(coroutine CXX)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# 搜索当前目录下所有文件
file(GLOB_RECURSE SRCS "*.cc" "*.cpp" "*.c" "*.h" "*.hpp")


IF(NOT CMAKE_CXX_COMPILER MATCHES "clang")
    #gcc
    SET(COROUTINE_LIB "-fcoroutines")
ELSE()
    #clang
    SET(COROUTINE_LIB "-fcoroutines-ts")
ENDIF()

add_compile_options("-g")


include_directories("../include")

# 生成库文件
add_executable(coroutine_test ${SRCS})

#指定库输出路径
SET(EXECUTABLE_OUTPUT_PATH ${BINARIES_TEST})

# 指定额外的链接库
TARGET_LINK_LIBRARIES(coroutine_test
PUBLIC ${COROUTINE_LIB}
 Threads::Threads
)
