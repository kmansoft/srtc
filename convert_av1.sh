#!/bin/bash

ffmpeg -i sintel_trailer-720p.mp4 \
	-c:v av1_nvenc \
	-preset p4 \
	-b:v 1M \
	-an \
	sintel-av1.webm
