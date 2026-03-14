cmake_minimum_required(VERSION 3.31.10)
set(CMAKE_MODULE_PATH "/home/lex/Documents/trading_app/build_backend/_deps/eigen-src/cmake")
project(CheckFortran Fortran)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/result.cmake"
  "set(CMAKE_Fortran_COMPILER \"${CMAKE_Fortran_COMPILER}\")\n"
  
  )