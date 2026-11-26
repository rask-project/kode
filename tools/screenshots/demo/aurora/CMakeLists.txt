cmake_minimum_required(VERSION 3.21)
project(aurora VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(aurora src/main.cpp)
qt_add_qml_module(aurora
    URI Aurora
    SOURCES src/Forecast.h src/Forecast.cpp src/Reading.h
    QML_FILES qml/Main.qml qml/ForecastView.qml qml/HourCard.qml
)
target_link_libraries(aurora PRIVATE Qt6::Quick)
