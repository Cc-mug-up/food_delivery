cmake_minimum_required(VERSION 3.16)
project(food_delivery_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- MySQL 配置 ----
set(MYSQL_DIR "C:/Program Files/MySQL/MySQL Server 8.0")
if(NOT EXISTS "${MYSQL_DIR}/include/mysql.h")
    # 尝试自动查找
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

# ---- 源文件 ----
set(SERVER_SOURCES
    src/main.cpp
    src/server.cpp
    src/order_handler.cpp
)

# ---- 可执行文件 ----
add_executable(food_delivery_server ${SERVER_SOURCES})

target_include_directories(food_delivery_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MYSQL_INCLUDE_DIRS}
)

target_link_libraries(food_delivery_server PRIVATE
    ${MYSQL_LIBRARIES}
)

# Windows 需要额外的库
if(WIN32)
    target_link_libraries(food_delivery_server PRIVATE ws2_32)
endif()

# Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(food_delivery_server PRIVATE pthread)
endif()

# 构建选项
if(MSVC)
    target_compile_options(food_delivery_server PRIVATE /W3)
else()
    target_compile_options(food_delivery_server PRIVATE -Wall -Wextra -O2)
endif()

# 复制 config.json 和前端到构建目录
add_custom_command(TARGET food_delivery_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:food_delivery_server>/config.json"
)

message(STATUS "MySQL include: ${MYSQL_INCLUDE_DIRS}")
message(STATUS "MySQL library: ${MYSQL_LIBRARIES}")
