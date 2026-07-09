cmake_minimum_required(VERSION 3.16)
project(MySunRemoteClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebSockets)

if(NOT DEFINED WEBRTC_ROOT)
    message(WARNING "WEBRTC_ROOT belgilanmagan — webrtcmanager.cpp kompilyatsiya qilinmaydi!")
endif()

set(SOURCES
    src/main.cpp
    src/inputcontrol.cpp
    src/screencapture.cpp
    src/webrtcmanager.cpp
    src/mainwindow.cpp
)

set(HEADERS
    src/inputcontrol.h
    src/screencapture.h
    src/webrtcmanager.h
    src/mainwindow.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebSockets
)

if(WEBRTC_ROOT)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${WEBRTC_ROOT}
        ${WEBRTC_ROOT}/third_party/abseil-cpp
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${WEBRTC_ROOT}/out/Default/obj/libwebrtc.a
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE WEBRTC_POSIX)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE user32)
elseif(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${X11_LIBRARIES} Xtst)
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework CoreGraphics"
        "-framework Carbon"
    )
endif()
