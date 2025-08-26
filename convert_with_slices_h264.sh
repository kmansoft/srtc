#!/bin/bash

ffmpeg -i sintel_trailer-720p.mp4 \
	-c:v libx264 \
	-x264opts slices=2 \
	-profile:v baseline \
	-level:v 3.1 \
	sintel_with_slices.h264
