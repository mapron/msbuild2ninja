#[[
  Copyright (C) 2017 Smirnov Vladimir mapron1@gmail.com
  Source code licensed under the Apache License, Version 2.0 (the "License");
  You may not use this file except in compliance with the License.
  You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0 or in file COPYING-APACHE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.h
#]]

cmake_minimum_required(VERSION 3.15)
project(Msbuild2ninja)

#platform configuration.
if (MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX /wd4267 /wd4244)
	set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /O2")
endif()
set(CMAKE_CXX_STANDARD 17)

set(sys_deps)
if ( WIN32)
	list(APPEND sys_deps)
else()
	list(APPEND sys_deps -lpthread)
endif()

if (APPLE)
	list(APPEND sys_deps -stdlib=libc++ -lc++)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	list(APPEND sys_deps  -lc++experimental)  # Libc++experimental.a may not always be available, even when libc++ is already installed
else()
	if (NOT MSVC)
		list(APPEND sys_deps  -lstdc++fs)
	endif()
endif()

# function for target declaration.
function(AddTarget)
	set(options APP)
	set(oneValueArgs NAME ROOT SUBDIR)
	set(multiValueArgs CSRC INCLUDES DEPS EXCLUDE OPTIONS DEFINES)
	cmake_parse_arguments(AddTarget "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )
	set(_sources)
	if (NOT AddTarget_ROOT)
		set(AddTarget_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/${AddTarget_SUBDIR}${AddTarget_NAME})
	endif()
	foreach (csrc ${AddTarget_CSRC})
		file(GLOB src ${AddTarget_ROOT}/${csrc})
		list(APPEND _sources ${src})
	endforeach()
	foreach (excl ${AddTarget_EXCLUDE})
		list(REMOVE_ITEM _sources ${AddTarget_ROOT}/${excl})
	endforeach()
	if (AddTarget_APP)
		add_executable(${AddTarget_NAME} ${_sources})
	else()
		add_library(${AddTarget_NAME} STATIC ${_sources})
	endif()
	foreach (inc ${AddTarget_INCLUDES})
		target_include_directories(${AddTarget_NAME} PRIVATE ${inc})
	endforeach()
	target_include_directories(${AddTarget_NAME} PUBLIC ${AddTarget_ROOT})
	foreach (dep ${AddTarget_DEPS})
		target_link_libraries(${AddTarget_NAME} PRIVATE ${dep})
	endforeach()
	foreach (opt ${AddTarget_OPTIONS})
		target_compile_options(${AddTarget_NAME} PRIVATE ${opt})
	endforeach()
	foreach (opt ${AddTarget_DEFINES})
		target_compile_definitions(${AddTarget_NAME} PRIVATE ${opt})
	endforeach()
endfunction()

AddTarget(APP NAME Msbuild2ninja ROOT src/ CSRC *.cpp *.h)
