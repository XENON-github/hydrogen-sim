CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -std=gnu11 -Iinclude -Iexternal
PKG_CFLAGS ?= $(shell pkg-config --cflags gtk4 cairo)
PKG_LIBS   ?= $(shell pkg-config --libs gtk4 cairo)
LIBS    = $(PKG_LIBS) -lm -pthread

SRC = src/main.c src/app.c src/view.c src/ui.c src/hud.c src/renderer.c src/colors.c \
      src/camera.c src/math_utils.c src/hydrogen.c src/sampling.c
OBJ = $(SRC:src/%.c=build/%.o) build/glad.o
TARGET = build/hydrogen_simulator
HEADERS = $(wildcard include/*.h)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LIBS)

build/%.o: src/%.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

build/glad.o: external/glad.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# GTK-free physics/sampler unit tests
test: tests/test_physics.c src/hydrogen.c src/sampling.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/test_physics $^ -lm -pthread
	./build/test_physics

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build

.PHONY: all test run clean
