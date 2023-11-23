cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(nkurdt)

file(GLOB_RECURSE NKURDT_LIB_SRC "nkurdt/src/*.cpp")
file(GLOB_RECURSE NKURDT_LIB_HDR "nkurdt/include/*.hpp")

set(NKURDT_INC_DIR "nkurdt/include")
set(NKURDT_APP_INC_DIR "app/")

file(GLOB_RECURSE FILE_SENDER_SRC "app/file_transfer/sender.cpp")
file(GLOB_RECURSE FILE_RECEIVER_SRC "app/file_transfer/receiver.cpp")

add_library(nkurdt STATIC ${NKURDT_LIB_SRC} ${NKURDT_LIB_HDR})

target_include_directories(nkurdt PUBLIC ${NKURDT_INC_DIR})

add_executable(file_sender ${FILE_SENDER_SRC})
add_executable(file_receiver ${FILE_RECEIVER_SRC})

target_include_directories(file_sender PUBLIC ${NKURDT_INC_DIR}
                                              ${NKURDT_APP_INC_DIR})
target_include_directories(file_receiver PUBLIC ${NKURDT_INC_DIR}
                                                ${NKURDT_APP_INC_DIR})

target_link_libraries(file_sender nkurdt)
target_link_libraries(file_receiver nkurdt)

# asan if debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(nkurdt PUBLIC -fsanitize=address)
  target_link_options(nkurdt PUBLIC -fsanitize=address)
  target_compile_options(file_sender PUBLIC -fsanitize=address)
  target_link_options(file_sender PUBLIC -fsanitize=address)
  target_compile_options(file_receiver PUBLIC -fsanitize=address)
  target_link_options(file_receiver PUBLIC -fsanitize=address)
elseif(NOT WIN32)
  target_compile_options(nkurdt PUBLIC -O2)
  target_compile_options(file_sender PUBLIC -O2)
  target_compile_options(file_receiver PUBLIC -O2)
endif()

if(WIN32)
  target_link_libraries(file_sender ws2_32)
  target_link_libraries(file_receiver ws2_32)
endif()
