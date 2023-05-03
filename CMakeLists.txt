cmake_minimum_required(VERSION 3.13...3.24)

project("Test Python" LANGUAGES CXX)

file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/test_runner.py ${CMAKE_CURRENT_BINARY_DIR}/test_runner.py
  COPY_ON_ERROR
)
