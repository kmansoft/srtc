cmake_minimum_required(VERSION 3.22.1)

project("srtc")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_library(srtc STATIC
        include/srtc/peer_connection.h
        include/srtc/sdp_offer.h
        include/srtc/srtc.h
        peer_connection.cpp
        sdp_offer.cpp
        srtc.cpp
)

target_include_directories(srtc PRIVATE "./include")