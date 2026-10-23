cmake_minimum_required(VERSION 3.20)
project(DCompressor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(LibArchive REQUIRED)

add_executable(dcompressor src/main.cpp)
target_link_libraries(dcompressor PRIVATE Qt6::Widgets LibArchive::LibArchive)

if(MSVC)
	target_compile_options(dcompressor PRIVATE /W4 /permissive- /utf-8)
else()
	target_compile_options(dcompressor PRIVATE -Wall -Wextra -Wpedantic)
endif()