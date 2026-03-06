cmake_minimum_required(VERSION 3.10.1)
project(tte)

include_directories(include)

set(CMAKE_CXX_STANDARD 20) #wajib jika menggunakan subproject yang make concepts
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -funroll-loops -ffast-math -fno-exceptions -fno-rtti -flto")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx")

add_executable(tte ${SRC})
