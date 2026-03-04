cmake_minimum_required(VERSION 3.10.1)
project(tte)

include_directories(include)

set(CMAKE_CXX_STANDARD 20) #wajib jika menggunakan subproject yang make concepts
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -funroll-loops -ffast-math -fopenmp")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(tte src/main.cxx)
