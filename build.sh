#!/bin/sh
echo "Building frfix.so..."
echo gcc -DCHANGERES -DFIXDELAY=1024 -Wall -fPIC -shared -m32 -Os -s `pkg-config --cflags alsa` -o frfix.so frfix.c "$@"
gcc -DCHANGERES -DFIXDELAY=1024 -Wall -fPIC -shared -m32 -Os -s `pkg-config --cflags alsa` -o frfix.so frfix.c "$@" && echo "Done."
