cmake_minimum_required(VERSION 4.3.0)

set(TARGET_NAME disable-alt-space)

project(${TARGET_NAME} LANGUAGES CXX)

if (NOT WIN32)
	message(FATAL_ERROR "This project must be built for Windows.")
endif()

if (
	NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
	OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
)
	message(FATAL_ERROR "${TARGET_NAME} must be built with clang-cl.")
endif()

add_executable(${TARGET_NAME}
	src/main.cpp
)

set_target_properties(${TARGET_NAME} PROPERTIES
	CXX_STANDARD 26
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if (
	NOT CMAKE_BUILD_TYPE STREQUAL "Debug"
	AND NOT CMAKE_BUILD_TYPE STREQUAL "Release"
)
	message(FATAL_ERROR "Unsupported build type: '${CMAKE_BUILD_TYPE}'. Only \"Debug\" and \"Release\" are allowed.")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL")
else()
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

target_compile_options(${TARGET_NAME} PRIVATE
	/clang:-Wall
	/clang:-Wextra
	$<$<CONFIG:Debug>:/Zi; /Ob0; /clang:-O0>
	$<$<CONFIG:Release>:/Ob3; /GS-; /sdl-; /guard:cf-; /clang:-O3; /clang:-flto=thin; /clang:-fwhole-program-vtables; /clang:-fno-rtti; /clang:-fno-stack-protector>
)