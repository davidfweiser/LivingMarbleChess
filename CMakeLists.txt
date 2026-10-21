cmake_minimum_required(VERSION 3.18)
project(LivingMarble VERSION 1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  Living Marble - 3D Battle Chess
#  All dependencies are fetched automatically (internet needed for 1st build).
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LM_ENABLE_AUDIO "Build with procedural sound effects (miniaudio)" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)
# CMake 4.x refuses very old cmake_minimum_required() in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# --- GLFW (window + input) -------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
  # X11 works everywhere (incl. XWayland). Turn this ON if you have the
  # wayland-scanner / xkbcommon dev packages and want native Wayland.
  option(LM_GLFW_WAYLAND "Build GLFW with native Wayland support" OFF)
  set(GLFW_BUILD_WAYLAND ${LM_GLFW_WAYLAND} CACHE BOOL "" FORCE)
  set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE)

# --- GLM (math) --------------------------------------------------------------
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
  GIT_SHALLOW    TRUE)

# --- Dear ImGui (HUD) ------------------------------------------------------
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.9b
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _no_cmake_)

FetchContent_MakeAvailable(glfw glm imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# --- miniaudio (sound) -------------------------------------------------------
if(LM_ENABLE_AUDIO)
  FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.22
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _no_cmake_)
  FetchContent_MakeAvailable(miniaudio)
endif()

# --- The game ----------------------------------------------------------------
add_executable(living_marble
  src/main.cpp
  src/app.cpp
  src/gl.cpp
  src/chess.cpp
  src/ai.cpp
  src/sdf.cpp
  src/meshgen.cpp
  src/medieval_mesh.cpp
  src/medieval_pose.cpp
  src/textures.cpp
  src/renderer.cpp
  src/shaders.cpp
  src/animation.cpp
  src/battle.cpp
  src/battle_medieval.cpp
  src/particles.cpp
  src/audio.cpp
  src/ui.cpp
  src/image_io.cpp)

target_include_directories(living_marble PRIVATE src)
target_link_libraries(living_marble PRIVATE glfw glm::glm imgui)
target_compile_definitions(living_marble PRIVATE GLFW_INCLUDE_NONE GLM_FORCE_SILENT_WARNINGS)

if(LM_ENABLE_AUDIO)
  target_include_directories(living_marble PRIVATE ${miniaudio_SOURCE_DIR})
  target_compile_definitions(living_marble PRIVATE LM_AUDIO=1)
endif()

find_package(Threads REQUIRED)
target_link_libraries(living_marble PRIVATE Threads::Threads)

if(APPLE)
  target_link_libraries(living_marble PRIVATE
    "-framework OpenGL" "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
  target_link_libraries(living_marble PRIVATE ${CMAKE_DL_LIBS} m)
endif()

if(MSVC)
  target_compile_options(living_marble PRIVATE /W3 /O2 /fp:fast /MP)
  target_compile_definitions(living_marble PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT living_marble)
else()
  target_compile_options(living_marble PRIVATE -Wall -Wno-unused-parameter -Wno-unused-function)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(living_marble PRIVATE -O2)
  endif()
endif()

# --- Install -----------------------------------------------------------------
#   cmake --install build --prefix ~/.local    (just you, no sudo)
#   sudo cmake --install build                 (everyone, into /usr/local)
include(GNUInstallDirs)
install(TARGETS living_marble RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
  # launcher entry + icon; the window class (= window title) ties the window to them
  set(LM_APP_ID io.github.davidfweiser.LivingMarble)
  install(FILES linux/${LM_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  foreach(size 32 48 64 128 256 512)
    install(FILES linux/icons/${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME ${LM_APP_ID}.png)
  endforeach()
  # refresh the menu and icon caches so the launcher shows it right away
  # (skipped when staging a package with DESTDIR)
  install(CODE "set(LM_DATADIR \"${CMAKE_INSTALL_DATADIR}\")")
  install(CODE [[
    if("$ENV{DESTDIR}" STREQUAL "")
      execute_process(COMMAND gtk-update-icon-cache -q -t -f
                      "${CMAKE_INSTALL_PREFIX}/${LM_DATADIR}/icons/hicolor" ERROR_QUIET)
      execute_process(COMMAND update-desktop-database -q
                      "${CMAKE_INSTALL_PREFIX}/${LM_DATADIR}/applications" ERROR_QUIET)
    endif()
  ]])
endif()
