#!/bin/bash
# tar.sh: package frfix for distribution
# Copyright (C) 2012-2014, Ryan Pennucci <decimalman@gmail.com>

echo "building 32- and 64-bit libraries"
gcc -Wall -fPIC -shared -Os -m32 -s `pkg-config --cflags alsa` -lglut -lasound -lrt -o frfix-32.so.prebuilt frfix.c "$@"
gcc -Wall -fPIC -shared -Os -m64 -s `pkg-config --cflags alsa` -lglut -lasound -lrt -o frfix-64.so.prebuilt frfix.c "$@"
echo "tarring everything up..."
tar c README COPYING build.sh frfix.c frfix-32.so.prebuilt frfix-64.so.prebuilt launch.sh | bzip2 -9 > frfix-$(date +%F).tar.bz2
echo "done"
