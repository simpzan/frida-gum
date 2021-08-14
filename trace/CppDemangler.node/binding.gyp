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
