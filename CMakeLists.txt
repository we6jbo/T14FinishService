cmake_minimum_required(VERSION 3.21)

project(T14FinishService VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The original Qt Widgets template files remain in this source tree unchanged:
#   main.cpp, mainwindow.cpp, mainwindow.h, mainwindow.ui
# They are intentionally not compiled because this project is a background-only service.
find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_add_executable(T14FinishService
    service_main.cpp
    finishservice.cpp
    finishservice.h
)

target_link_libraries(T14FinishService PRIVATE
    Qt6::Core
    Qt6::Network
)

install(TARGETS T14FinishService
    RUNTIME DESTINATION bin
)
