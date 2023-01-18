# What is this?
This package is a one of a series packages intended to
support the development of general purpose chess engines and tools.

This package implements a basic minimax-tree-searching chess engine. This
implementation follows most of the techniques taught and explained in
https://www.chessprogramming.org/. This engine uses a static evaluation function
and serves as the basis for the [chess-neuralnet-engine](
https://github.com/leonkacowicz/chess-neuralnet-engine) which uses a neural-net
as its evaluation function.

# How do I build it?

This package uses CMake as a build-tool. In theory, it requires CMake 3.5,
but I haven't tested it with previous versions of CMake. If you manage to
build it with an older version of CMake, let me know or send a pull request
decreasing the required version on line 1 of `/CMakeLists.txt`

To build it:
```sh
mkdir build
cd build
cmake ..
make
```

# Testing
After building, an executable at `build/test/engine/engine_test` should be generated.
All tests specified in `/tests/` should be invoked by this executable.