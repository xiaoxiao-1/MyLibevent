project(MyLibevent)
cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
include_directories(./include/http/)
add_executable(main ./src/exec/main.cpp ./src/http/http.cpp)

target_link_libraries(main -levent)
