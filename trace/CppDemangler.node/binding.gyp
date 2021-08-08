{
  "targets": [
    {
      "target_name": "addon",
      "sources": [ "addon.cc" ],
      "include_dirs": [
        "libelfin",
      ],
      "libraries": [
        "<!(pwd)/libelfin/dwarf/libdwarf++.a",
        "<!(pwd)/libelfin/elf/libelf++.a"
      ],
      "cflags_cc": [ "-fexceptions" ]
    }
  ]
}
