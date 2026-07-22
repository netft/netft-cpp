cmake_minimum_required(VERSION 3.16)

project(netft_consumer LANGUAGES CXX)

find_package(netft 0.1 CONFIG REQUIRED)

if(DEFINED NETFT_EXPECTED_INCLUDE_DIR)
  get_target_property(netft_include_dirs
    netft::netft INTERFACE_INCLUDE_DIRECTORIES)
  list(LENGTH netft_include_dirs netft_include_dir_count)
  if(NOT netft_include_dir_count EQUAL 1)
    message(FATAL_ERROR
      "netft::netft must export exactly one include directory; got: ${netft_include_dirs}")
  endif()
  list(GET netft_include_dirs 0 netft_include_dir)
  if(NOT netft_include_dir STREQUAL NETFT_EXPECTED_INCLUDE_DIR)
    message(FATAL_ERROR
      "netft::netft include directory is '${netft_include_dir}', expected '${NETFT_EXPECTED_INCLUDE_DIR}'")
  endif()
endif()

add_executable(netft_consumer main.cpp)
target_link_libraries(netft_consumer PRIVATE netft::netft)
