#!/bin/bash

ffmpeg -i sintel_trailer-720p.mp4 \
	-c:v libaom-av1 \
	-usage realtime \
	-b:v 1M \
	-cpu-used 8 \
	-an \
	sintel-av1.webm
