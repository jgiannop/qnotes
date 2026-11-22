cmake_minimum_required(VERSION 3.16)
project(QNotes VERSION 1.3.0 LANGUAGES C CXX
        DESCRIPTION "Sticky notes with text and key-value notes"
        HOMEPAGE_URL "https://github.com/jgiannop/qnotes")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

qt_add_executable(qnotes
    main.cpp
    notecolors.h
    notemanager.h notemanager.cpp
    notewindow.h notewindow.cpp
    noteslistwindow.h noteslistwindow.cpp
    keyvalueeditor.h keyvalueeditor.cpp
    systemauth.h systemauth.cpp
    singleinstance.h singleinstance.cpp
    secretstore.h secretstore.cpp
    theme.h theme.cpp
    autostart.h autostart.cpp
    preferencesdialog.h preferencesdialog.cpp
    backup.h backup.cpp
    third_party/tweetnacl/tweetnacl.h third_party/tweetnacl/tweetnacl.c
)

qt_add_resources(qnotes "icons"
    PREFIX "/"
    FILES
        icons/qnotes-16.png
        icons/qnotes-32.png
        icons/qnotes-48.png
        icons/qnotes-64.png
        icons/qnotes-128.png
        icons/qnotes-256.png
)

target_compile_definitions(qnotes PRIVATE QNOTES_VERSION="${PROJECT_VERSION}")
target_link_libraries(qnotes PRIVATE Qt6::Widgets Qt6::Network)

# Secret values go to the system keychain via QtKeychain. Linux: distro package (qtkeychain-qt6-dev);
# if it isn't installed (e.g. on Windows) it's built from source as a static library.
find_package(Qt6Keychain QUIET)
if(Qt6Keychain_FOUND AND NOT QNOTES_BUNDLE_QTKEYCHAIN)
    target_link_libraries(qnotes PRIVATE ${QTKEYCHAIN_LIBRARIES})
    target_include_directories(qnotes PRIVATE ${QTKEYCHAIN_INCLUDE_DIRS})
else()
    include(FetchContent)
    set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(qtkeychain
        GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
        GIT_TAG 0.15.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(qtkeychain)
    target_link_libraries(qnotes PRIVATE qt6keychain)
    # Static library: its headers must not declare the API as dllimport on Windows.
    target_compile_definitions(qnotes PRIVATE QTKEYCHAIN_NO_EXPORT)
endif()

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

    # X11 tray detection, so a left click on the tray icon works on GNOME (see NoteManager::setupTray)
    find_library(XCB_LIBRARY xcb)
    find_path(XCB_INCLUDE_DIR xcb/xcb.h)
    if(XCB_LIBRARY AND XCB_INCLUDE_DIR)
        target_compile_definitions(qnotes PRIVATE HAVE_XCB)
        target_include_directories(qnotes PRIVATE ${XCB_INCLUDE_DIR})
        target_link_libraries(qnotes PRIVATE ${XCB_LIBRARY})
    endif()
endif()

# Windows: password check via LogonUser, exe icon + version info
if(WIN32)
    target_compile_definitions(qnotes PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(qnotes PRIVATE advapi32)
    set(QNOTES_ICO "${CMAKE_CURRENT_SOURCE_DIR}/icons/qnotes.ico")
    configure_file(packaging/windows/qnotes.rc.in qnotes.rc @ONLY)
    target_sources(qnotes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/qnotes.rc")
endif()

if(MSVC)
    target_compile_options(qnotes PRIVATE /utf-8)
endif()

set_target_properties(qnotes PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# ---------------------------------------------------------------- install / packaging

if(WIN32)
    install(TARGETS qnotes RUNTIME DESTINATION .)
    # Copies the Qt DLLs and plugins next to qnotes.exe (runs windeployqt).
    qt_generate_deploy_app_script(TARGET qnotes OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
    install(FILES README.md DESTINATION .)
elseif(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS qnotes RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/qnotes.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icons/qnotes.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    foreach(size 16 32 48 64 128 256)
        install(FILES icons/qnotes-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME qnotes.png)
    endforeach()
endif()

set(CPACK_PACKAGE_NAME "qnotes")
set(CPACK_PACKAGE_VENDOR "jgiannop")
set(CPACK_PACKAGE_CONTACT "jgiannop <19926058+jgiannop@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "QNotes")
set(CPACK_PACKAGE_EXECUTABLES "qnotes" "QNotes")
set(CPACK_STRIP_FILES ON)

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_PACKAGE_FILE_NAME "qnotes-${PROJECT_VERSION}-windows-x64")
    set(CPACK_NSIS_PACKAGE_NAME "QNotes")
    set(CPACK_NSIS_DISPLAY_NAME "QNotes")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icons/qnotes.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/icons/qnotes.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "qnotes.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP\\\\QNotes.lnk' '$INSTDIR\\\\qnotes.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\\\QNotes.lnk'")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)   # Qt / PAM dependencies via dpkg-shlibdeps
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")   # X11 platform plugin (not a library dep)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
        "Sticky notes with text and key-value notes\n Desktop sticky notes inspired by Windows Sticky Notes.\n Key-value notes can hold secret values that need the system password to reveal.")
endif()

include(CPack)
