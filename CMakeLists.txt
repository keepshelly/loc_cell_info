cmake_minimum_required(VERSION 3.16)
project(GpsTracker)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_DRAFTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)

add_subdirectory(third_party/json)
add_subdirectory(third_party/glfw)
add_subdirectory(third_party/libzmq)

set(IMGUI_DIR third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

find_package(OpenGL REQUIRED)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw ${OPENGL_LIBRARIES})

set(IMPLOT_DIR third_party/implot)
add_library(implot STATIC
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot PUBLIC imgui)

add_executable(monitor_server 
    src/main.cpp 
    src/Server.cpp
)

target_include_directories(monitor_server PRIVATE 
    include
    third_party
    third_party/cppzmq
    third_party/libzmq/include
)

find_package(Threads REQUIRED)
target_link_libraries(monitor_server PRIVATE 
    imgui 
    implot
    glfw 
    nlohmann_json::nlohmann_json
    libzmq-static
    Threads::Threads
    ${OPENGL_LIBRARIES}
    dl
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)

target_link_libraries(monitor_server PRIVATE 
    imgui 
    implot 
    glfw 
    nlohmann_json::nlohmann_json 
    libzmq-static 
    ${PQXX_LIBRARIES}  
    Threads::Threads 
    ${OPENGL_LIBRARIES} 
    dl
)
target_include_directories(monitor_server PRIVATE ${PQXX_INCLUDE_DIRS})