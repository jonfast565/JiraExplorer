cmake_minimum_required(VERSION 3.21)
project(JiraExplorerQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/mainwindow.ui
    src/settingsdialog.h
    src/settingsdialog.cpp
    src/settingsdialog.ui
    src/models.h
    src/config.h
    src/config.cpp
    src/error.h
    src/error.cpp
    src/jira_client.h
    src/jira_client.cpp
    src/datahub.h
    src/datahub.cpp
    src/ticketsmodel.h
    src/ticketsmodel.cpp
    resources/resources.qrc
)

qt_add_executable(JiraExplorerQt
    ${SOURCES}
)

target_link_libraries(JiraExplorerQt PRIVATE Qt6::Widgets Qt6::Network)

# On Windows, copy Qt DLLs next to the exe when building with MSVC (optional)
if (WIN32)
  set_property(TARGET JiraExplorerQt PROPERTY WIN32_EXECUTABLE TRUE)
endif()
