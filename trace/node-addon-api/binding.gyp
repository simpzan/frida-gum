{
  "targets": [
    {
      "target_name": "addon",
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "sources": [ "addon.cc", "myobject.cc", "elf_file.cpp", "utils.cpp" ],
      "libraries": [
        "<!(pwd)/../libelfin/dwarf/libdwarf++.a",
        "<!(pwd)/../libelfin/elf/libelf++.a"
      ],
      "include_dirs": [
        "../libelfin",
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
    }
  ]
}
