#!/bin/bash

ffmpeg -i sintel_trailer-720p.mp4 \
	-c:v libvpx \
	-pix_fmt yuv420p \
	-b:v 1M \
	-an \
	sintel-vp8.webm
