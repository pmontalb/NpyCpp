include_guard()

cmake_minimum_required(VERSION 3.28)
project(NpyCpp LANGUAGES C CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/All.cmake)

add_subdirectory(Npy++)

if (PROJECT_IS_TOP_LEVEL)
	add_subdirectory(cnpy)
	add_subdirectory(UnitTests)
endif ()
