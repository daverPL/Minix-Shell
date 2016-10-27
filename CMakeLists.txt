cmake_minimum_required(VERSION 3.6)
project(shell)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    include/builtins.h
    include/config.h
    include/siparse.h
    include/utils.h
        inputParse/Makefile
        inputParse/siparse.lex
        inputParse/siparse.y
        inputParse/siparseutils.c
        inputParse/siparseutils.h
    builtins.c
    Makefile
    mshell.c
    siparse.a
    utils.c)

add_executable(shell ${SOURCE_FILES})