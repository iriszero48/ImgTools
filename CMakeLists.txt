cmake_minimum_required(VERSION 3.9.0)
project(img VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_VERBOSE_MAKEFILE ON)

find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

aux_source_directory(src "src")
add_executable(img ${src})

target_include_directories(img PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(img PRIVATE
    Eigen3::Eigen
    opencv_core
    opencv_imgcodecs
    opencv_dnn
    opencv_highgui

    ${FFMPEG_LIBRARIES}
)
