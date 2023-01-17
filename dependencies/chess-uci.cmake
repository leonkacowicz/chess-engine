include(ExternalProject)
ExternalProject_Add(
        chess-uci-external
        URL https://github.com/leonkacowicz/chess-uci/archive/refs/heads/main.zip
        PREFIX ${CMAKE_BINARY_DIR}/chess-uci-external
        INSTALL_COMMAND ""
        LOG_DOWNLOAD ON
        LOG_CONFIGURE ON)

add_library(libchess-uci IMPORTED STATIC GLOBAL)
add_dependencies(libchess-uci chess-uci-external)
include_directories("${CMAKE_BINARY_DIR}/chess-uci-external/src/chess-uci-external/src/uci/include")
set_target_properties(libchess-uci PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/chess-uci-external/src/chess-uci-external/"
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/chess-uci-external/src/chess-uci-external-build/src/uci/libchess_uci.a"
        )
