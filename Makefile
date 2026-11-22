# Raccourcis ; la compilation elle-même est décrite dans CMakeLists.txt.
.PHONY: all clean rebuild run stats headless test

all:
	cmake -S . -B build
	cmake --build build

clean:
	cmake -E rm -rf build

rebuild: clean all

# La démo, dans une fenêtre
run: all
	./build/f1en32

# La même, avec les compteurs (toutes les 5 secondes, sur le terminal)
stats: all
	VDP_STATS=1 ./build/f1en32

# Sans fenêtre : 5 secondes, capture de la dernière image
headless: all
	./build/f1en32_headless --frames 300 --shot 299:build/capture.ppm

# Tous les tests (environ 10 secondes)
test:
	tests/run_tests.sh
