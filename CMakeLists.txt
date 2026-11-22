cmake_minimum_required(VERSION 3.16)
project(QNotes VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(qnotes
    main.cpp
    notecolors.h
    notemanager.h notemanager.cpp
    notewindow.h notewindow.cpp
    noteslistwindow.h noteslistwindow.cpp
    keyvalueeditor.h keyvalueeditor.cpp
    systemauth.h systemauth.cpp
)

target_link_libraries(qnotes PRIVATE Qt6::Widgets)

# System password check for secret values (Linux: PAM, needs libpam0g-dev)
if(UNIX AND NOT APPLE)
    find_library(PAM_LIBRARY pam)
    find_path(PAM_INCLUDE_DIR security/pam_appl.h)
    if(PAM_LIBRARY AND PAM_INCLUDE_DIR)
        target_compile_definitions(qnotes PRIVATE HAVE_PAM)
        target_include_directories(qnotes PRIVATE ${PAM_INCLUDE_DIR})
        target_link_libraries(qnotes PRIVATE ${PAM_LIBRARY})
    else()
        message(WARNING "PAM not found: secret values cannot be revealed")
    endif()
endif()

set_target_properties(qnotes PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
