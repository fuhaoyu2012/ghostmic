CC=gcc
CFLAGS=-Wall -Wextra -ggdb -std=c11
LIBS=`pkg-config --cflags --libs /mingw64/lib/pkgconfig/portaudio-2.0.PC`
DLL=/mingw64/bin/libportaudio.dll
te:main.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)
	cp $(DLL) .
clean:
	rm -rf *.exe *.dll
