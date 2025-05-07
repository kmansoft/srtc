#!/bin/bash

ffmpeg -i sintel_trailer-720p.mp4 \
	-c:v libx264 \
	-profile:v baseline \
	-level:v 3.1 \
	sintel.h264
