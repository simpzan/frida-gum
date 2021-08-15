all: run

elf: 
	make -C libelfin

addon: addon.cc log.h ./libelfin/elf/libelf++.a ./libelfin/dwarf/libdwarf++.a
	node-gyp rebuild --debug

run: elf addon
	node srcline-test.js ~/frida/libsurfaceflinger.so