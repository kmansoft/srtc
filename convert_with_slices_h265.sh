#!/bin/bash

ffmpeg -i sintel_trailer-720p.mp4 \
	-c:v libx265 \
	-x265-params "slices=2" \
	-crf 23 \
	-c:a copy \
	sintel_with_slices.h265
