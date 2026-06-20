cmake_minimum_required(VERSION 3.16)
project(food_delivery_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- MySQL 配置 (与 server 相同) ----
set(MYSQL_DIR "C:/Program Files/MySQL/MySQL Server 8.0")
if(NOT EXISTS "${MYSQL_DIR}/include/mysql.h")
    find_path(MYSQL_INCLUDE_DIR mysql.h
        PATHS "C:/Program Files/MySQL/MySQL Server 8.0/include"
              "/usr/include/mysql"
              "/usr/local/mysql/include"
    )
    find_library(MYSQL_LIBRARY libmysql
        PATHS "C:/Program Files/MySQL/MySQL Server 8.0/lib"
              "/usr/lib"
              "/usr/local/mysql/lib"
    )
    set(MYSQL_INCLUDE_DIRS ${MYSQL_INCLUDE_DIR})
    set(MYSQL_LIBRARIES ${MYSQL_LIBRARY})
else()
    set(MYSQL_INCLUDE_DIRS "${MYSQL_DIR}/include")
    set(MYSQL_LIBRARIES "${MYSQL_DIR}/lib/libmysql.lib")
endif()

# ---- Include ----
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/include
    ${MYSQL_INCLUDE_DIRS}
)

# ---- 测试可执行文件 ----

# 1. 数据库连接池测试
add_executable(test_db test_db.cpp)
target_link_libraries(test_db PRIVATE ${MYSQL_LIBRARIES})
if(WIN32)
    target_link_libraries(test_db PRIVATE ws2_32)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(test_db PRIVATE pthread)
endif()

# 2. 订单 CRUD 测试 (需要 server/src/order_handler.cpp 编译)
add_executable(test_order
    test_order.cpp
    ../server/src/order_handler.cpp
)
target_link_libraries(test_order PRIVATE ${MYSQL_LIBRARIES})
if(WIN32)
    target_link_libraries(test_order PRIVATE ws2_32)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(test_order PRIVATE pthread)
endif()

# 3. 并发压力测试 (纯 socket, 不需要 MySQL)
add_executable(test_concurrency test_concurrency.cpp)
if(WIN32)
    target_link_libraries(test_concurrency PRIVATE ws2_32)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(test_concurrency PRIVATE pthread)
endif()

# 4. 性能基准测试
add_executable(benchmark benchmark.cpp)
if(WIN32)
    target_link_libraries(benchmark PRIVATE ws2_32)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(benchmark PRIVATE pthread)
endif()

# 构建选项
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
endif()

message(STATUS "Tests CMake configured")
