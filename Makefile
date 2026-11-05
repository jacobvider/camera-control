# Convenience wrapper around CMake, ctest and the Python tooling.
BUILD ?= build
SAN   ?= build-sanitize
CXX_SOURCES := $(shell find server -name "*.cpp" -o -name "*.hpp")
CMAKE_FLAGS ?=

.PHONY: all build test test-cpp test-python lint format sanitize clean

all: test-cpp

build:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSUPERLOTIS_WERROR=ON $(CMAKE_FLAGS)
	cmake --build $(BUILD) -j

test-cpp: build
	cd $(BUILD) && ctest --output-on-failure -j4

test-python: build
	cd python && SLCAMD_BIN=$(abspath $(BUILD))/server/slcamd python3 -m pytest

test: test-cpp test-python

lint:
	clang-format --dry-run --Werror $(CXX_SOURCES)
	cd python && ruff check . && ruff format --check . && mypy

format:
	clang-format -i $(CXX_SOURCES)
	cd python && ruff format . && ruff check --fix .

sanitize:
	cmake -S . -B $(SAN) -DCMAKE_BUILD_TYPE=Debug -DSUPERLOTIS_SANITIZE=ON $(CMAKE_FLAGS)
	cmake --build $(SAN) -j
	cd $(SAN) && ctest --output-on-failure -j4

clean:
	rm -rf $(BUILD) $(SAN)
