cmake_minimum_required(VERSION 3.16)

project(F1ExhaustNote32 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ---- Simulateur 32X « pixel » ----------------------------------------------
# VDP 32X, VDP Mega Drive (sprites), interface (COMM, V_INT, H_INT, FM) et
# plates-formes : sans rapport avec le moteur.

set(PIXEL_SOURCES pixel/vdp.c pixel/interface.c pixel/mdvdp.c)

# Fenêtre : MiniFB (sous-module git). Sans lui, seules les cibles sans
# fenêtre sont construites (tests, captures).
set(HAVE_MINIFB OFF)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/minifb/CMakeLists.txt)
	enable_language(CXX)
	if(APPLE)
		enable_language(OBJC)
	endif()
	set(MINIFB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(MINIFB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	add_subdirectory(minifb)
	set(HAVE_MINIFB ON)
else()
	message(STATUS "MiniFB absent (git submodule update --init) : pas de fenetre")
endif()

# ---- Outils de l'hôte : images et circuit -> données C ----------------------
add_executable(bmp2c tools/bmp2c.c)
target_include_directories(bmp2c PRIVATE race)
target_compile_options(bmp2c PRIVATE -Wall -Wextra)

add_executable(track2c tools/track2c.c)
target_include_directories(track2c PRIVATE race)
target_compile_options(track2c PRIVATE -Wall -Wextra)
target_link_libraries(track2c PRIVATE m)

# ---- Données de la démo ----------------------------------------------------
# Converties à chaque modification d'une source.

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(DEMO_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/assets.txt)
file(GLOB DEMO_BMP CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/props/*.bmp)
set(DEMO_TRACK ${CMAKE_CURRENT_SOURCE_DIR}/demo/track.txt)

add_custom_command(
	OUTPUT ${GENERATED}/demo_assets.c ${GENERATED}/demo_assets.h
	COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
	COMMAND bmp2c ${DEMO_MANIFEST} ${GENERATED}/demo_assets
	DEPENDS bmp2c ${DEMO_MANIFEST} ${DEMO_BMP}
	COMMENT "Conversion des images de la demo"
)

add_custom_command(
	OUTPUT ${GENERATED}/demo_track.c ${GENERATED}/demo_track.h
	COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
	COMMAND track2c ${DEMO_TRACK} ${GENERATED}/demo_track demo_track
	DEPENDS track2c ${DEMO_TRACK}
	COMMENT "Conversion du circuit de la demo"
)

# Données de la démo, partagées par les exécutables
add_library(demo_data STATIC ${GENERATED}/demo_assets.c ${GENERATED}/demo_track.c)
target_include_directories(demo_data PUBLIC race ${GENERATED})

# ---- Moteur + démo ---------------------------------------------------------

set(RACE_ENGINE race/project.c race/render.c race/hud.c race/sprites.c)
set(RACE_SOURCES
	race/race.c
	race/framebuffer.c
	${RACE_ENGINE}
	demo/content.c
	demo/md68k.c
	${PIXEL_SOURCES}
)
set(RACE_INCLUDES pixel race ${GENERATED})

# Avec fenêtre
if(HAVE_MINIFB)
	add_executable(f1en32 ${RACE_SOURCES} pixel/platform_minifb.c)
	target_include_directories(f1en32 PRIVATE ${RACE_INCLUDES})
	target_compile_options(f1en32 PRIVATE -Wall -Wextra)
	target_link_libraries(f1en32 PRIVATE demo_data minifb Threads::Threads)
	set_target_properties(f1en32 PROPERTIES LINKER_LANGUAGE CXX)
endif()

# Sans fenêtre : tests et captures (voir pixel/platform_headless.c)
add_executable(f1en32_headless ${RACE_SOURCES} pixel/platform_headless.c)
target_include_directories(f1en32_headless PRIVATE ${RACE_INCLUDES})
target_compile_options(f1en32_headless PRIVATE -Wall -Wextra)
target_link_libraries(f1en32_headless PRIVATE demo_data Threads::Threads)

# ---- Tests -----------------------------------------------------------------
# Le moteur et le programme 68000, sans le simulateur : test_race fournit
# lui-même le frame buffer, les registres COMM et la manette.

add_executable(test_race tests/test_race.c ${RACE_ENGINE} demo/content.c demo/md68k.c)
target_include_directories(test_race PRIVATE ${RACE_INCLUDES})
target_compile_options(test_race PRIVATE -Wall -Wextra)
target_link_libraries(test_race PRIVATE demo_data)

enable_testing()
add_test(NAME race COMMAND test_race)
