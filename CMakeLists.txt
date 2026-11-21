cmake_minimum_required(VERSION 3.16)

project(SuperVDP C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ---- Simulateur 32X « pixel » ----------------------------------------------
# VDP, interface (COMM, V_INT) et plates-formes : sans rapport avec SuperVDP.

set(PIXEL_SOURCES pixel/vdp.c pixel/interface.c)

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

# ---- Outil : BMP 8 bits -> données C ---------------------------------------

add_executable(bmp2c tools/bmp2c.c)
target_include_directories(bmp2c PRIVATE supervdp)
target_compile_options(bmp2c PRIVATE -Wall -Wextra)

# ---- Images de la démo -----------------------------------------------------
# Converties à chaque modification du manifeste ou d'un BMP.

set(DEMO_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/assets.txt)
file(GLOB DEMO_BMP CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/*.bmp)
set(DEMO_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/demo_assets)

add_custom_command(
	OUTPUT ${DEMO_GENERATED}.c ${DEMO_GENERATED}.h
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
	COMMAND bmp2c ${DEMO_MANIFEST} ${DEMO_GENERATED}
	DEPENDS bmp2c ${DEMO_MANIFEST} ${DEMO_BMP}
	COMMENT "Conversion des images de la demo"
)

# Données de la démo, partagées par les exécutables
add_library(demo_assets STATIC ${DEMO_GENERATED}.c)
target_include_directories(demo_assets PUBLIC supervdp ${CMAKE_CURRENT_BINARY_DIR}/generated)

# ---- SuperVDP + démo -------------------------------------------------------

set(SUPERVDP_SOURCES
	supervdp/supervdp.c
	supervdp/compose.c
	supervdp/framebuffer.c
	demo/content.c
	demo/md68k.c
	${PIXEL_SOURCES}
)
set(SUPERVDP_INCLUDES pixel supervdp ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Avec fenêtre
if(HAVE_MINIFB)
	add_executable(supervdp ${SUPERVDP_SOURCES} pixel/platform_minifb.c)
	target_include_directories(supervdp PRIVATE ${SUPERVDP_INCLUDES})
	target_compile_options(supervdp PRIVATE -Wall -Wextra)
	target_link_libraries(supervdp PRIVATE demo_assets minifb Threads::Threads)
	set_target_properties(supervdp PROPERTIES LINKER_LANGUAGE CXX)
endif()

# Sans fenêtre : tests et captures (voir pixel/platform_headless.c)
add_executable(supervdp_headless ${SUPERVDP_SOURCES} pixel/platform_headless.c)
target_include_directories(supervdp_headless PRIVATE ${SUPERVDP_INCLUDES})
target_compile_options(supervdp_headless PRIVATE -Wall -Wextra)
target_link_libraries(supervdp_headless PRIVATE demo_assets Threads::Threads)

# ---- Tests -----------------------------------------------------------------

add_executable(test_compose tests/test_compose.c supervdp/compose.c)
target_include_directories(test_compose PRIVATE ${SUPERVDP_INCLUDES})
target_compile_options(test_compose PRIVATE -Wall -Wextra)
target_link_libraries(test_compose PRIVATE demo_assets)

enable_testing()
add_test(NAME compose COMMAND test_compose)
