cmake_minimum_required(VERSION 3.21)
project(imgui-smash CXX)

## Error if not using switch toolchain file
if (NOT SWITCH AND NOT IMGUI_SMASH_EXTERNAL_TOOLCHAIN)
  message(FATAL_ERROR "Not targeting switch, make sure to specify -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake, or set IMGUI_SMASH_EXTERNAL_TOOLCHAIN if another toolchain is already active")
endif ()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 23)


## ImGui user config
add_compile_definitions(IMGUI_USER_CONFIG="imgui_user_config.h")

## Dear ImGui: the extern/imgui submodule's sources with this fork's patches (patches/imgui/*.patch, in name order)
## applied to a copy in the build tree. The submodule itself stays as upstream has it.
find_package(Git REQUIRED)
set(IMGUI_UPSTREAM_DIR ${PROJECT_SOURCE_DIR}/extern/imgui)
set(IMGUI_DIR ${CMAKE_BINARY_DIR}/imgui-patched)
file(GLOB IMGUI_UPSTREAM_FILES ${IMGUI_UPSTREAM_DIR}/*.cpp ${IMGUI_UPSTREAM_DIR}/*.h)
file(GLOB IMGUI_PATCHES ${PROJECT_SOURCE_DIR}/patches/imgui/*.patch)
list(SORT IMGUI_PATCHES)
file(REMOVE_RECURSE ${IMGUI_DIR})
file(COPY ${IMGUI_UPSTREAM_FILES} DESTINATION ${IMGUI_DIR})
foreach(IMGUI_PATCH ${IMGUI_PATCHES})
  # GIT_CEILING_DIRECTORIES keeps git apply from treating an enclosing repository as the patch root.
  execute_process(
          COMMAND ${CMAKE_COMMAND} -E env GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}
                  ${GIT_EXECUTABLE} apply --whitespace=nowarn ${IMGUI_PATCH}
          WORKING_DIRECTORY ${IMGUI_DIR}
          RESULT_VARIABLE IMGUI_PATCH_RESULT
          ERROR_VARIABLE IMGUI_PATCH_ERROR)
  if(NOT IMGUI_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Dear ImGui patch ${IMGUI_PATCH} does not apply to ${IMGUI_UPSTREAM_DIR}: ${IMGUI_PATCH_ERROR}")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${IMGUI_UPSTREAM_FILES} ${IMGUI_PATCHES})

## Project sources
include_directories(${PROJECT_SOURCE_DIR}/src_framework/lib)
include_directories(${PROJECT_SOURCE_DIR}/src_framework/nvn)
include_directories(${PROJECT_SOURCE_DIR}/src_framework/nn)
include_directories(${PROJECT_SOURCE_DIR}/src_framework/)

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${IMGUI_DIR})
include_directories(${PROJECT_SOURCE_DIR}/fonts/precompiled)
include_directories(${PROJECT_SOURCE_DIR}/shaders/precompiled)
include_directories(${PROJECT_SOURCE_DIR}/user_config)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCES_ASM
        ${PROJECT_SOURCE_DIR}/src/*.s
        ${PROJECT_SOURCE_DIR}/src_framework/*.s
        )
file(GLOB_RECURSE SOURCES_H
        ${PROJECT_SOURCE_DIR}/src/*.h ${PROJECT_SOURCE_DIR}/src/*.hpp
        ${PROJECT_SOURCE_DIR}/src_framework/*.h ${PROJECT_SOURCE_DIR}/src_framework/*.hpp
        )
file(GLOB_RECURSE SOURCES_CXX
        ${PROJECT_SOURCE_DIR}/src/*.cpp ${PROJECT_SOURCE_DIR}/src/*.c
        ${PROJECT_SOURCE_DIR}/src_framework/*.cpp ${PROJECT_SOURCE_DIR}/src_framework/*.c
        )
file(GLOB IMGUI_SOURCES ${IMGUI_DIR}/*.cpp)
file(GLOB_RECURSE LOCAL_LIBRARIES ${PROJECT_SOURCE_DIR}/libs/*.a)

file(GLOB SOURCES_H ${PROJECT_SOURCE_DIR}/fonts/precompiled/*.h)
file(GLOB SOURCES_H ${PROJECT_SOURCE_DIR}/shaders/precompiled/*.h)
file(GLOB SOURCES_H ${PROJECT_SOURCE_DIR}/user_config/*.h)

## Include nx tools
if (NOT IMGUI_SMASH_EXTERNAL_TOOLCHAIN)
  include(${CMAKE_SOURCE_DIR}/cmake/SwitchTools.cmake)
endif ()

## Build static lib
add_library(imgui_smash STATIC ${SOURCES_ASM} ${SOURCES_C} ${SOURCES_H} ${SOURCES_CXX} ${IMGUI_SOURCES})
set_target_properties(imgui_smash PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
target_link_libraries(imgui_smash ${LOCAL_LIBRARIES})
if(DEFINED IMGUI_SMASH_INITIAL_COMMAND_BUDGET)
  target_compile_definitions(imgui_smash PRIVATE IMGUI_SMASH_INITIAL_COMMAND_BUDGET=${IMGUI_SMASH_INITIAL_COMMAND_BUDGET})
endif()
if(DEFINED IMGUI_SMASH_INITIAL_CONTROL_BUDGET)
  target_compile_definitions(imgui_smash PRIVATE IMGUI_SMASH_INITIAL_CONTROL_BUDGET=${IMGUI_SMASH_INITIAL_CONTROL_BUDGET})
endif()
if(DEFINED IMGUI_SMASH_RESERVE_COMMAND_BUDGET)
  target_compile_definitions(imgui_smash PRIVATE IMGUI_SMASH_RESERVE_COMMAND_BUDGET=${IMGUI_SMASH_RESERVE_COMMAND_BUDGET})
endif()
if(DEFINED IMGUI_SMASH_RESERVE_CONTROL_BUDGET)
  target_compile_definitions(imgui_smash PRIVATE IMGUI_SMASH_RESERVE_CONTROL_BUDGET=${IMGUI_SMASH_RESERVE_CONTROL_BUDGET})
endif()
