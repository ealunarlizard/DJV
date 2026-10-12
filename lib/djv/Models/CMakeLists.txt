set(HEADERS
    AnnotationsModel.h
    AppInfoModel.h
    AudioModel.h
    ColorModel.h
    CommandsModel.h
    Credentials.h
    DeepLink.h
    DrawModel.h
    Export.h
    FilesModel.h
    LanModel.h
    MarkersModel.h
    NetworkDrive.h
    OCIOModel.h
    Parse.h
    Playlist.h
    RecentFilesModel.h
    Review.h
    Stroke.h
    SyncModel.h
    SettingsModel.h
    ShellCommand.h
    Shortcuts.h
    TimeUnitsModel.h
    ToolsModel.h
    Version.h
    ViewportModel.h)
set(HEADERS_PRIVATE)

set(SOURCE
    AnnotationsModel.cpp
    AppInfoModel.cpp
    AudioModel.cpp
    ColorModel.cpp
    CommandsModel.cpp
    Credentials.cpp
    DeepLink.cpp
    DrawModel.cpp
    FilesModel.cpp
    LanModel.cpp
    MarkersModel.cpp
    NetworkDrive.cpp
    OCIOModel.cpp
    Parse.cpp
    Playlist.cpp
    RecentFilesModel.cpp
    Review.cpp
    Stroke.cpp
    SyncModel.cpp
    SettingsModel.cpp
    ShellCommand.cpp
    Shortcuts.cpp
    TimeUnitsModel.cpp
    ToolsModel.cpp
    ViewportModel.cpp)

# The commit and build date, refreshed on every build rather than only when
# the project is configured: a hash that is one commit stale is worse than
# none, since the point of it is telling two builds apart.
set(BUILD_INFO_H ${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.h)
set(BUILD_INFO_ARGS
    -DSRC_DIR=${PROJECT_SOURCE_DIR}
    -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/BuildInfo.h.in
    -DOUT_FILE=${BUILD_INFO_H}
    -P ${PROJECT_SOURCE_DIR}/cmake/Modules/BuildInfo.cmake)
execute_process(COMMAND ${CMAKE_COMMAND} ${BUILD_INFO_ARGS})
add_custom_target(djvBuildInfo
    COMMAND ${CMAKE_COMMAND} ${BUILD_INFO_ARGS}
    BYPRODUCTS ${BUILD_INFO_H}
    COMMENT "Build information")
set_target_properties(djvBuildInfo PROPERTIES FOLDER lib)

add_library(djvModels ${HEADERS} ${HEADERS_PRIVATE} ${SOURCE})
# Private when building, so this library's own API is dllexport here and
# dllimport to everything else. Public when static, which a consumer does
# have to know: the API then carries no declspec at all.
if(BUILD_SHARED_LIBS)
    target_compile_definitions(djvModels PRIVATE DJV_MODELS_EXPORTS)
else()
    target_compile_definitions(djvModels PUBLIC DJV_MODELS_STATIC)
endif()
add_dependencies(djvModels djvBuildInfo)

target_include_directories(djvModels
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}/deps/cpp-httplib)

# tlUI for the thumbnail system: a shared djvModels must name every
# library it uses directly, where the static build let the final link
# resolve it.
target_link_libraries(djvModels djvResource tlRender::tlUI tlRender::tlTimeline)

# The sync client (SyncModel.cpp): cpp-httplib, a single header kept in deps,
# runs its connections on threads of its own, and on Windows needs the
# sockets library. MSVC is told by the header; MinGW is not.
find_package(Threads REQUIRED)
target_link_libraries(djvModels Threads::Threads)
if(WIN32)
    target_link_libraries(djvModels ws2_32)
endif()

# Mapping the studio's media share to a drive letter (NetworkDrive.cpp) is the
# Windows networking API, WNetAddConnection2, which lives in mpr. The passwords
# DJV keeps (Credentials.cpp) are in the Credential Manager, in advapi32, as
# are the registry and the security descriptors the review links
# (DeepLink.cpp) use; letting the running DJV come to the front is user32.
if(WIN32)
    target_link_libraries(djvModels mpr advapi32 user32)
endif()
set_property(GLOBAL APPEND PROPERTY LEGAL_NOTICES
    "cpp-httplib=${PROJECT_SOURCE_DIR}/deps/cpp-httplib/LICENSE")

set_target_properties(djvModels PROPERTIES FOLDER lib)
set_target_properties(djvModels PROPERTIES PUBLIC_HEADER "${HEADERS}")

install(
    TARGETS djvModels
    EXPORT djvModelsTargets
    ARCHIVE DESTINATION lib COMPONENT dev
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/djv/Models COMPONENT dev)
install(
    EXPORT djvModelsTargets
    FILE djvModelsTargets.cmake
    DESTINATION "lib/cmake/djv"
    NAMESPACE djv::
    COMPONENT dev)
