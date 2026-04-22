#!/bin/bash

ffmpeg -i sintel_trailer-720p.mp4 \
	-c:v libvpx-vp9 \
	-profile:v 0 \
	-pix_fmt yuv420p \
	-b:v 1M \
	-an \
	sintel-vp9.webm
