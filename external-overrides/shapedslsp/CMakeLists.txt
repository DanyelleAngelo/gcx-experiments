cmake_minimum_required(VERSION 2.6)

### use a compiler supporting constexpr math
# set(CMAKE_C_COMPILER "/opt/local/bin/gcc-mp-9" CACHE STRING "gcc compiler" FORCE)
# set(CMAKE_CXX_COMPILER "/opt/local/bin/gcc-mp-9" CACHE STRING "g++ compiler" FORCE)

### Set a default build type if none was specified
link_directories(/home/danyelleangelo/lib)

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

project(ShapedSlp)

include_directories(${PROJECT_SOURCE_DIR})
set(FOLCA_SOURCE_DIR ${PROJECT_SOURCE_DIR}/folca)
include_directories(${FOLCA_SOURCE_DIR})
set(SUX_SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/sux/sux)
include_directories(${SUX_SOURCE_DIR}/function)
include_directories(${SUX_SOURCE_DIR}/support)

### check for SDSL
find_library(SDSL_LIB sdsl)
if (NOT SDSL_LIB)
  message("sdsl-lite not found, building from submodule (hopefully you did git clone --recursive)")
  add_subdirectory(external/sdsl-lite)
  include_directories(${CMAKE_BINARY_DIR}/external/sdsl-lite/include/)
  include_directories(${CMAKE_BINARY_DIR}/external/sdsl-lite/external/libdivsufsort/include)
  LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/external/sdsl-lite/lib)
  LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/external/sdsl-lite/external/libdivsufsort/lib)
else()
  find_library(DSS divsufsort)
  find_library(DSS64 divsufsort64)
  message("sdsl: " ${SDSL_LIB})
  message("divsufsort: " ${DSS})
  message("divsufsort64: " ${DSS64})

  include_directories(/home/danyelleangelo/include)
endif()

include_directories(${CMAKE_BINARY_DIR}/external/sdsl-lite/include/)
include_directories(${CMAKE_BINARY_DIR}/external/sdsl-lite/external/libdivsufsort/include)


message("Building in ${CMAKE_BUILD_TYPE} mode")

# set(CMAKE_CXX_FLAGS "--std=c++2a /opt/local/lib/libstdc++.6.dylib")
# set(CMAKE_CXX_FLAGS "--std=c++2a")

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -std=c++17 -ggdb -g -march=native -pthread")
set(CMAKE_CXX_FLAGS_RELEASE "-g -std=c++17 -Ofast -fstrict-aliasing -march=native -pthread -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g -std=c++17 -ggdb -Ofast -fstrict-aliasing -march=native -pthread")


add_executable(SlpEncBuild SlpEncBuild.cpp ${FOLCA_SOURCE_DIR}/fbtree.cpp ${FOLCA_SOURCE_DIR}/onlinebp.c)
TARGET_LINK_LIBRARIES(SlpEncBuild sdsl)

add_executable(SubstrBenchmark SubstrBenchmark.cpp ${FOLCA_SOURCE_DIR}/fbtree.cpp ${FOLCA_SOURCE_DIR}/onlinebp.c)
TARGET_LINK_LIBRARIES(SubstrBenchmark sdsl)

add_executable(ExtractBenchmark ExtractBenchmark.cpp ${FOLCA_SOURCE_DIR}/fbtree.cpp ${FOLCA_SOURCE_DIR}/onlinebp.c)
TARGET_LINK_LIBRARIES(ExtractBenchmark sdsl)

add_executable(LceBenchmark LceBenchmark.cpp ${FOLCA_SOURCE_DIR}/fbtree.cpp ${FOLCA_SOURCE_DIR}/onlinebp.c)
TARGET_LINK_LIBRARIES(LceBenchmark sdsl)

target_include_directories(SlpEncBuild PRIVATE
  ${PROJECT_SOURCE_DIR}/external/sdsl-lite/include
  ${PROJECT_SOURCE_DIR}/external/sdsl-lite/external/libdivsufsort/include
)


