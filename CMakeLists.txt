cmake_minimum_required(VERSION 3.9.0)
project(img VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")

add_definitions(/bigobj)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:console /NODEFAULTLIB:library")
add_definitions(-DUNICODE -D_UNICODE)

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_path(STB_INCLUDE_DIRS "stb_image.h")

aux_source_directory(src "src")
add_executable(img WIN32 ${src})
target_compile_definitions(img PRIVATE
    STB_IMAGE_IMPLEMENTATION
    STBI_WINDOWS_UTF8
    STB_IMAGE_WRITE_IMPLEMENTATION)

target_include_directories(img PRIVATE ${STB_INCLUDE_DIRS})

target_link_libraries(img PRIVATE
    imgui::imgui
    nlohmann_json nlohmann_json::nlohmann_json

    d3d11.lib
    d3dcompiler.lib
    dxgi.lib
    msvcrt.lib
)
