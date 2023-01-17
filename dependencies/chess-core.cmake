include(ExternalProject)
ExternalProject_Add(
        chess-core-external
        URL https://github.com/leonkacowicz/chess-core/archive/refs/heads/main.zip
        PREFIX ${CMAKE_BINARY_DIR}/chess-core-external
        INSTALL_COMMAND ""
        LOG_DOWNLOAD ON
        LOG_CONFIGURE ON)

add_library(libchess-core IMPORTED STATIC GLOBAL)
add_dependencies(libchess-core chess-core-external)
include_directories("${CMAKE_BINARY_DIR}/chess-core-external/src/chess-core-external/src/chess/include")
set_target_properties(libchess-core PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/chess-core-external/src/chess-core-external/"
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/chess-core-external/src/chess-core-external-build/src/chess/libchess.a"
        )
