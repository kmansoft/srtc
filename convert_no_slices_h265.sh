#!/bin/bash

ffmpeg -i sintel_trailer-720p.mp4 \
	-c:v libx265 \
	-crf 23 \
	-g 30 \
	-c:a copy \
	sintel.h265
