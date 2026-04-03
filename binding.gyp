{
  "targets": [
    {
      "target_name": "express_pro_native",
      "sources": [
        "src/binding.c",
        "src/router.c",
        "src/parser.c",
        "src/fast_router.c"
      ],
      "cflags": [
        "-O3",
        "-funroll-loops",
        "-fomit-frame-pointer"
      ],
      "cflags_cc": [
        "-O3",
        "-funroll-loops",
        "-fomit-frame-pointer"
      ],
      "conditions": [
        [
          "OS=='linux'",
          {
            "sources": [
              "src/server_uring.c"
            ],
            "defines": [
              "USE_IO_URING"
            ],
            "libraries": [
              "-luring"
            ]
          }
        ],
        [
          "OS=='mac'",
          {
            "sources": [
              "src/server_kevent.c"
            ],
            "defines": [
              "USE_KQUEUE"
            ],
            "xcode_settings": {
              "GCC_OPTIMIZATION_LEVEL": "3",
              "OTHER_CFLAGS": [
                "-funroll-loops",
                "-fomit-frame-pointer"
              ]
            }
          }
        ],
        [
          "OS=='win'",
          {
            "sources": [
              "src/server_iocp.c"
            ],
            "defines": [
              "USE_IOCP",
              "WIN32_LEAN_AND_MEAN",
              "_WIN32_WINNT=0x0600"
            ],
            "msvs_settings": {
              "VCCLCompilerTool": {
                "Optimization": 3,
                "InlineFunctionExpansion": 2,
                "EnableIntrinsicFunctions": "true",
                "FavorSizeOrSpeed": 1
              }
            }
          }
        ],
        [
          "OS!='linux' and OS!='mac' and OS!='win'",
          {
            "sources": [
              "src/server_libuv.c"
            ],
            "defines": [
              "USE_LIBUV"
            ]
          }
        ]
      ]
    }
  ]
}
