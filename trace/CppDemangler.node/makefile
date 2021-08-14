all: run

elf: 
	make -C libelfin

./build/Release/addon.node: addon.cc log.h ./libelfin/elf/libelf++.a ./libelfin/dwarf/libdwarf++.a 
	/usr/local/bin/npm install

addon: ./build/Release/addon.node
	@echo

run: elf addon
	node srcline-test.js ~/frida/libsurfaceflinger.so