cmake_minimum_required(VERSION 3.20)
project(LE2BRShaderTool LANGUAGES CXX)

if(NOT DEFINED LE2BR_PATCHER_DIR)
  message(FATAL_ERROR "Set LE2BR_PATCHER_DIR to the external BlackRestoration source directory")
endif()

add_executable(le2br-shader-tool
  main.cpp
  "${LE2BR_PATCHER_DIR}/DxbcPatcher.cpp"
)
target_include_directories(le2br-shader-tool PRIVATE "${LE2BR_PATCHER_DIR}/..")
target_compile_features(le2br-shader-tool PRIVATE cxx_std_20)
