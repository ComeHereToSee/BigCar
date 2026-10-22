# BigCar engine-agnostic core: builds Source/CrashCore (the same sources Unreal compiles) plus unit tests and the
# headless battle simulator. This is what GitHub Actions runs on every push (no Unreal Engine required).
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(BigCarCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BIGCAR_WARNINGS_AS_ERRORS "Treat warnings as errors (mirrors Unreal's strictness)" ON)
option(BIGCAR_SANITIZE "Build with address + undefined behaviour sanitizers" OFF)

file(GLOB CRASHCORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/CrashCore/Private/*.cpp)
list(FILTER CRASHCORE_SOURCES EXCLUDE REGEX "CrashCoreModule\\.cpp$")
file(GLOB CRASHCORE_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/CrashCore/Public/CrashCore/*.h)

function(bigcar_warnings target)
	if(MSVC)
		# Unreal builds without exceptions/RTTI and treats shadowing (C4456-C4459) as errors.
		# Like Unreal: implicit numeric conversions (C4244/C4267/C4305) are not errors, shadowing is.
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /GR- /w44456 /w44457 /w44458 /w44459
			/wd4244 /wd4267 /wd4305 /wd4100 /wd4127)
		target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0 NOMINMAX)
		if(BIGCAR_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE /WX)
		endif()
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wundef -Wnon-virtual-dtor -Wnull-dereference
			-fno-exceptions -fno-rtti)
		if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			target_compile_options(${target} PRIVATE -Wshadow-all -Wunused-private-field)
		endif()
		if(BIGCAR_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()
	if(BIGCAR_SANITIZE AND NOT MSVC)
		target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(${target} PRIVATE -fsanitize=address,undefined)
	endif()
endfunction()

add_library(CrashCore STATIC ${CRASHCORE_SOURCES} ${CRASHCORE_HEADERS})
target_include_directories(CrashCore PUBLIC ${CMAKE_SOURCE_DIR}/Source/CrashCore/Public)
bigcar_warnings(CrashCore)
if(BIGCAR_SANITIZE AND NOT MSVC)
	target_link_options(CrashCore INTERFACE -fsanitize=address,undefined)
endif()

enable_testing()

file(GLOB CORE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Tests/Core/*.cpp)
add_executable(CrashCoreTests ${CORE_TEST_SOURCES})
target_link_libraries(CrashCoreTests PRIVATE CrashCore)
target_compile_definitions(CrashCoreTests PRIVATE
	BIGCAR_DATA_DIR="${CMAKE_SOURCE_DIR}/Content/BigCar/Data"
	BIGCAR_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/Tests/Core/fixtures"
	BIGCAR_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Source")
bigcar_warnings(CrashCoreTests)
add_test(NAME CrashCoreTests COMMAND CrashCoreTests)

add_executable(crashsim ${CMAKE_SOURCE_DIR}/Tools/crashsim/main.cpp)
target_link_libraries(crashsim PRIVATE CrashCore)
bigcar_warnings(crashsim)
add_test(NAME crashsim_smoke COMMAND crashsim --data ${CMAKE_SOURCE_DIR}/Content/BigCar/Data --smoke)
