cmake_minimum_required(VERSION 3.10)
project(nanoclib VERSION 0.1.0 LANGUAGES C)

include_directories(./src)
add_compile_options(-Wall -Wextra)

set(NANOC_CORE
    ./src/nc_scanner.c
    ./src/nc_compiler.c
    ./src/nc_runtime.c
    ./src/nc_memory.c
    ./src/nc_kvstore.c
    ./src/nanoc_debug.c
    ./src/nanoc_host.c
    ./src/nanoc_techage.c
    ./src/nanoc_net.c
)

# Standalone host hooks for a native test/CLI; the Lua C module is built by
# LuaRocks (see nanoc-0.1.0-1.rockspec) instead.
add_library(nanoccore STATIC ${NANOC_CORE})
