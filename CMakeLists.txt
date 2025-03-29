cmake_minimum_required(VERSION 3.14)
project(BookReader)

set(CMAKE_CXX_STANDARD 17)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DJVU REQUIRED ddjvuapi)
pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_executable(${PROJECT_NAME}
    mainwindow.h
    mainwindow.cpp
    imagelabel.h
    imagelabel.cpp
    searchdialog.h
    searchdialog.cpp
    main.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE /usr/include/libdjvu)

# target_include_directories(DjVuReader PRIVATE ${DJVU_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${DJVU_LIBRARIES}
        PkgConfig::POPPLER
        Qt6::Widgets
)
