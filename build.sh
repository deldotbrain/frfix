#!/bin/sh
# build.sh: configure & build/copy frfix
# Copyright (C) 2012-2014, Ryan Pennucci <decimalman@gmail.com>

echo "Building frfix.so..."
if file [Ff]ieldrunners 2>/dev/null | grep -q '64-bit'
then cflags=-m64; src=frfix-64.so.prebuilt
else cflags=-m32; src=frfix-32.so.prebuilt
fi
if gcc -Wall -fPIC -shared -Os -s `pkg-config --cflags alsa` -lglut -lasound -lrt $cflags -o frfix.so frfix.c "$@"
then	echo "Done."
else	echo "Failed.  Falling back to prebuilt frfix.so..."
	if cp $src frfix.so
	then	echo "Done."
	else	echo "Failed.  Do you have permissions to write to the current directory?"
	fi
fi
