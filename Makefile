# Hydrogen Atom Simulator
#
#   make            build the app          make test     GTK-free unit tests
#   make run        build + run            make gltest   headless OpenGL render checks (Linux, needs EGL + cairo)
#
# Platforms: Linux, macOS (Homebrew: brew install gtk4 pkg-config), Windows via MSYS2 MINGW64
# (pacman -S mingw-w64-x86_64-{toolchain,gtk4,pkgconf} make).

CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -std=gnu11 -Iinclude -Iexternal

UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
ifneq (,$(findstring MINGW,$(UNAME_S))$(findstring MSYS,$(UNAME_S))$(findstring CYGWIN,$(UNAME_S)))
  PLATFORM = windows
  EXE = .exe
  PLAT_LIBS =
  # WINDOWS_GUI=1 hides the console window
  ifeq ($(WINDOWS_GUI),1)
    PLAT_LDFLAGS = -mwindows
  endif
else ifeq ($(UNAME_S),Darwin)
  PLATFORM = macos
  EXE =
  PLAT_LIBS =
else
  PLATFORM = linux
  EXE =
  PLAT_LIBS = -ldl
endif

PKG_CFLAGS ?= $(shell pkg-config --cflags gtk4 cairo)
PKG_LIBS   ?= $(shell pkg-config --libs gtk4 cairo)
LIBS    = $(PKG_LIBS) -lm -pthread $(PLAT_LIBS)

SRC = src/main.c src/app.c src/view.c src/ui.c src/hud.c src/renderer.c src/colors.c \
      src/camera.c src/math_utils.c src/hydrogen.c src/sampling.c \
      src/platform.c src/superposition.c src/transitions.c src/volume.c src/gifenc.c src/recorder.c
OBJ = $(SRC:src/%.c=build/%.o) build/glad.o
TARGET = build/hydrogen_simulator$(EXE)
HEADERS = $(wildcard include/*.h)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(PLAT_LDFLAGS) $(LIBS)

build/%.o: src/%.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

build/glad.o: external/glad.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# ---- GTK-free unit tests (physics, transitions, superposition sampler, volume grid, GIF/MP4 export) ----
PHYS = src/hydrogen.c src/platform.c
build/test_physics$(EXE): tests/test_physics.c $(PHYS) src/sampling.c src/superposition.c src/transitions.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^ -lm -pthread
build/test_transitions$(EXE): tests/test_transitions.c $(PHYS) src/transitions.c src/superposition.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^ -lm
build/test_super$(EXE): tests/test_super.c $(PHYS) src/sampling.c src/volume.c src/superposition.c src/transitions.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^ -lm -pthread
build/test_export$(EXE): tests/test_export.c src/gifenc.c src/recorder.c src/platform.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^ -lm

test: build/test_physics$(EXE) build/test_transitions$(EXE) build/test_super$(EXE) build/test_export$(EXE)
	./build/test_physics
	./build/test_transitions
	./build/test_super
	./build/test_export build

# ---- headless OpenGL tests: real renderer + shaders + HUD through EGL (Mesa llvmpipe is fine) ----
GLTEST_SCENES = dens_4dz2 wave_cplx slice_3s spheres_2pz iso_3dz2 iso_wave_3dz2 iso_cplx_wheel iso_3s_cut \
                iso_both_3dz2 super_dens_t0 super_dens_t2 super_wave_wheel isosup_dens isosup_wave super_dens_three
build/gl_harness: tests/gl_harness.c tests/scene.c tests/scene_extra.c src/renderer.c src/hud.c src/colors.c \
                  src/camera.c src/math_utils.c src/hydrogen.c src/sampling.c src/platform.c \
                  src/superposition.c src/transitions.c src/volume.c src/recorder.c src/gifenc.c external/glad.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -Itests $(shell pkg-config --cflags cairo) -o $@ $(filter %.c,$^) \
	  $(shell pkg-config --libs cairo) -lm -pthread -ldl

gltest: build/gl_harness
	@mkdir -p build/gltest
	@for s in $(GLTEST_SCENES); do ./build/gl_harness $$s build/gltest/$$s.png 640 440 || exit 1; done
	@echo "all headless GL scenes rendered (images in build/gltest/)"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build

.PHONY: all test gltest run clean
