# Checkers XL: build with Retro68 (68000 code: Mac Plus ROM and up, so MacWorks Plus and Plus II).
#   bash build.sh
cmake_minimum_required(VERSION 3.10)
project(CheckersXL C)
add_application(CheckersXL
    src/main.c
    src/rules.c
    src/net.c
    src/mailbox.c
    src/xlproto.c
    src/ckxl.r
    TYPE "APPL"
    CREATOR "CKXL"
    )
target_include_directories(CheckersXL PRIVATE src)
set_target_properties(CheckersXL PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-Os;-Wall;-Wno-multichar;-Wno-pointer-sign;-Wno-array-bounds;-Dsprintf=siprintf;-Dsnprintf=sniprintf;-Dsscanf=siscanf")
set_target_properties(CheckersXL PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
