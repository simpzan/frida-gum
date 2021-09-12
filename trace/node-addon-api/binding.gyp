{
  "targets": [
    {
      "target_name": "addon",
      "sources": [ "addon.cc", "myobject.cc", "elf_file.cpp", "elf_debug_info.cpp", "utils.cpp" ],
      "libraries": [
        "<!(pwd)/../libelfin/dwarf/libdwarf++.a",
        "<!(pwd)/../libelfin/elf/libelf++.a"
      ],
      "include_dirs": [
        "../libelfin",
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      'conditions': [
        ['OS=="linux"', {
          "cflags_cc": [ "-fexceptions", "-frtti" ]
        }],
        ['OS=="mac"', {
          'xcode_settings': {
            'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
            'OTHER_CFLAGS': [ '-ObjC++' ]
          },
          'libraries': [ '-lobjc' ],
        }]
      ]
    }
  ]
}
