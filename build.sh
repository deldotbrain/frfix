#!/bin/sh
echo "Building frfix.so..."
if gcc -Wall -fPIC -shared -m32 -Os -s `pkg-config --cflags alsa` -o frfix.so frfix.c "$@"
then	echo "Done."
else	echo "Failed.  Falling back to prebuilt frfix.so..."
	mv frfix.so.prebuilt frfix.so || \
		echo "Failed.  Do you have permissions to write to the current directory?"
fi
