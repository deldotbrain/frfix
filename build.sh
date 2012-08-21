#!/bin/sh
echo "Building frfix.so..."
echo gcc -DFIXDELAY=1024 -Wall -fPIC -shared -m32 -Os -s `pkg-config --cflags alsa` "$@" frfix.c -o frfix.so
gcc -DFIXDELAY=1024 -Wall -fPIC -shared -m32 -Os -s `pkg-config --cflags alsa` "$@" frfix.c -o frfix.so && echo "Done."
