cmake_minimum_required(VERSION 3.22.1)

project("libogg")

add_library(ogg STATIC
        src/bitwise.c
        src/framing.c
)

target_include_directories(ogg
        PUBLIC
        include
)