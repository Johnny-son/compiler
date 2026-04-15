cmake_minimum_required(VERSION 3.20)

project(compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE COMPILER_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(compiler ${COMPILER_SOURCES})

target_include_directories(compiler PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ir/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
)
