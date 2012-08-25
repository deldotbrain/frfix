#!/bin/sh
echo "Building frfix.so..."
if file [Ff]ieldrunners 2>/dev/null | grep -q '64-bit'
then cflags=-m64; src=frfix-64.so.prebuilt
else cflags=-m32; src=frfix-32.so.prebuilt
fi
if gcc -Wall -fPIC -shared -Os -s `pkg-config --cflags alsa` $cflags -o frfix.so frfix.c "$@"
then	echo "Done."
else	echo "Failed.  Falling back to prebuilt frfix.so..."
	mv $src frfix.so || \
		echo "Failed.  Do you have permissions to write to the current directory?"
fi
