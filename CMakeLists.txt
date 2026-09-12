# T14FinishService CMake build definition
# ---------------------------------------
# Maintenance notes:
# - This is intentionally a background/console Qt application, not a Qt Widgets GUI.
# - The original Qt Creator template files remain in the repository unchanged but
#   are NOT listed in qt_add_executable(), so they are not compiled.
# - Add new service .cpp/.h files to qt_add_executable() below.
# - Add a Qt module to find_package() and target_link_libraries() if new code needs it.
cmake_minimum_required(VERSION 3.21)

project(T14FinishService VERSION 0.8 LANGUAGES CXX)

# C++17 is the project's baseline. AUTOMOC is needed because FinishService uses
# QObject/Q_OBJECT and Qt signals/slots.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Runtime dependencies used by the background service:
#   Core    -> event loop, JSON, files, dates/times, processes, storage, etc.
#   Network -> TCP localhost service and HTTP(S) forecast/update requests.
find_package(Qt6 REQUIRED COMPONENTS Core Network)

# Only background-service sources belong here. Do not add mainwindow.ui or the
# preserved Qt Widgets template unless the project is intentionally redesigned as GUI.
qt_add_executable(T14FinishService
    service_main.cpp
    finishservice.cpp
    finishservice.h
)

# Keep linked modules aligned with find_package() above.
target_link_libraries(T14FinishService PRIVATE
    Qt6::Core
    Qt6::Network
)

# Standard runtime installation target for packaging tools.
install(TARGETS T14FinishService
    RUNTIME DESTINATION bin
)
