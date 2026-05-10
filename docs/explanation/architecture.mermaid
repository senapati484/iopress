flowchart TD

subgraph group_grp_js["JavaScript API"]
  node_n_index_js["Entry API<br/>js entry<br/>[index.js]"]
  node_n_js_index["JS Facade<br/>js wrapper<br/>[index.js]"]
  node_n_js_deprecation["Deprecations<br/>js compat<br/>[deprecation.js]"]
  node_n_types["Types<br/>d.ts surface<br/>[index.d.ts]"]
end

subgraph group_grp_native["Native Core"]
  node_n_binding_gyp["Addon Build<br/>gyp config<br/>[binding.gyp]"]
  node_n_binding_c["Node Binding<br/>addon bridge<br/>[binding.c]"]
  node_n_main_cpp["Addon Main<br/>addon init<br/>[main.cpp]"]
  node_n_server_c["Server Core<br/>http runtime<br/>[server.c]"]
  node_n_parser["Parser<br/>[parser.c]"]
  node_n_router["Router<br/>dispatch<br/>[router.c]"]
  node_n_fast_router["Fast Router<br/>native fast-path<br/>[fast_router.c]"]
  node_n_error["Errors<br/>error handling<br/>[error_handling.c]"]
  node_n_common_server["Shared Server<br/>shared core<br/>[server.cpp]"]
  node_n_common_util["Shared Utils<br/>[util.cpp]"]
end

subgraph group_grp_platform["Platform Backends"]
  node_n_linux_io_uring["io_uring<br/>linux io<br/>[io_uring.cpp]"]
  node_n_linux_epoll["epoll fallback<br/>linux fallback<br/>[epoll_fallback.cpp]"]
  node_n_macos_kqueue["kqueue<br/>macos io<br/>[kqueue.cpp]"]
  node_n_windows_iocp["IOCP<br/>windows io<br/>[iocp.cpp]"]
  node_n_server_uring["Uring Server<br/>backend server<br/>[server_uring.c]"]
  node_n_server_kevent["Kevent Server<br/>backend server<br/>[server_kevent.c]"]
  node_n_server_iocp["IOCP Server<br/>backend server<br/>[server_iocp.c]"]
  node_n_server_libuv["Libuv Server<br/>compat backend<br/>[server_libuv.c]"]
end

subgraph group_grp_tooling["Build & Validation"]
  node_n_examples["Examples<br/>sample apps"]
  node_n_benchmarks["Benchmarks<br/>perf harness"]
  node_n_prebuild_test["Prebuild Check<br/>release test<br/>[test-prebuilds.js]"]
end

node_n_index_js -->|"exports"| node_n_js_index
node_n_js_index -->|"warns"| node_n_js_deprecation
node_n_js_index -.->|"describes"| node_n_types
node_n_js_index -->|"loads"| node_n_binding_c
node_n_binding_gyp -->|"builds"| node_n_binding_c
node_n_binding_gyp -->|"builds"| node_n_main_cpp
node_n_binding_c -->|"initializes"| node_n_main_cpp
node_n_main_cpp -->|"starts"| node_n_server_c
node_n_server_c -->|"parses"| node_n_parser
node_n_server_c -->|"dispatches"| node_n_router
node_n_router -->|"accelerates"| node_n_fast_router
node_n_server_c -->|"handles"| node_n_error
node_n_server_c -->|"shares"| node_n_common_server
node_n_common_server -->|"uses"| node_n_common_util
node_n_server_c -.->|"selects"| node_n_server_uring
node_n_server_c -.->|"selects"| node_n_server_kevent
node_n_server_c -.->|"selects"| node_n_server_iocp
node_n_server_c -.->|"falls back"| node_n_server_libuv
node_n_server_uring -->|"uses"| node_n_linux_io_uring
node_n_server_uring -->|"falls back"| node_n_linux_epoll
node_n_server_kevent -->|"uses"| node_n_macos_kqueue
node_n_server_iocp -->|"uses"| node_n_windows_iocp
node_n_js_index -.->|"showcases"| node_n_examples
node_n_server_c -.->|"measured by"| node_n_benchmarks
node_n_binding_gyp -.->|"validates"| node_n_prebuild_test

click node_n_index_js "https://github.com/senapati484/iopress/blob/main/index.js"
click node_n_js_index "https://github.com/senapati484/iopress/blob/main/js/index.js"
click node_n_js_deprecation "https://github.com/senapati484/iopress/blob/main/js/deprecation.js"
click node_n_types "https://github.com/senapati484/iopress/blob/main/index.d.ts"
click node_n_binding_gyp "https://github.com/senapati484/iopress/blob/main/binding.gyp"
click node_n_binding_c "https://github.com/senapati484/iopress/blob/main/src/binding.c"
click node_n_main_cpp "https://github.com/senapati484/iopress/blob/main/src/main.cpp"
click node_n_server_c "https://github.com/senapati484/iopress/blob/main/src/server.c"
click node_n_parser "https://github.com/senapati484/iopress/blob/main/src/parser.c"
click node_n_router "https://github.com/senapati484/iopress/blob/main/src/router.c"
click node_n_fast_router "https://github.com/senapati484/iopress/blob/main/src/fast_router.c"
click node_n_error "https://github.com/senapati484/iopress/blob/main/src/error_handling.c"
click node_n_common_server "https://github.com/senapati484/iopress/blob/main/src/common/server.cpp"
click node_n_common_util "https://github.com/senapati484/iopress/blob/main/src/common/util.cpp"
click node_n_linux_io_uring "https://github.com/senapati484/iopress/blob/main/src/platform/linux/io_uring.cpp"
click node_n_linux_epoll "https://github.com/senapati484/iopress/blob/main/src/platform/linux/epoll_fallback.cpp"
click node_n_macos_kqueue "https://github.com/senapati484/iopress/blob/main/src/platform/macos/kqueue.cpp"
click node_n_windows_iocp "https://github.com/senapati484/iopress/blob/main/src/platform/windows/iocp.cpp"
click node_n_server_uring "https://github.com/senapati484/iopress/blob/main/src/server_uring.c"
click node_n_server_kevent "https://github.com/senapati484/iopress/blob/main/src/server_kevent.c"
click node_n_server_iocp "https://github.com/senapati484/iopress/blob/main/src/server_iocp.c"
click node_n_server_libuv "https://github.com/senapati484/iopress/blob/main/src/server_libuv.c"
click node_n_examples "https://github.com/senapati484/iopress/tree/main/examples"
click node_n_benchmarks "https://github.com/senapati484/iopress/tree/main/benchmarks"
click node_n_prebuild_test "https://github.com/senapati484/iopress/blob/main/scripts/test-prebuilds.js"

classDef toneNeutral fill:#f8fafc,stroke:#334155,stroke-width:1.5px,color:#0f172a
classDef toneBlue fill:#dbeafe,stroke:#2563eb,stroke-width:1.5px,color:#172554
classDef toneAmber fill:#fef3c7,stroke:#d97706,stroke-width:1.5px,color:#78350f
classDef toneMint fill:#dcfce7,stroke:#16a34a,stroke-width:1.5px,color:#14532d
classDef toneRose fill:#ffe4e6,stroke:#e11d48,stroke-width:1.5px,color:#881337
classDef toneIndigo fill:#e0e7ff,stroke:#4f46e5,stroke-width:1.5px,color:#312e81
classDef toneTeal fill:#ccfbf1,stroke:#0f766e,stroke-width:1.5px,color:#134e4a
class node_n_index_js,node_n_js_index,node_n_js_deprecation,node_n_types toneBlue
class node_n_binding_gyp,node_n_binding_c,node_n_main_cpp,node_n_server_c,node_n_parser,node_n_router,node_n_fast_router,node_n_error,node_n_common_server,node_n_common_util toneAmber
class node_n_linux_io_uring,node_n_linux_epoll,node_n_macos_kqueue,node_n_windows_iocp,node_n_server_uring,node_n_server_kevent,node_n_server_iocp,node_n_server_libuv toneMint
class node_n_examples,node_n_benchmarks,node_n_prebuild_test toneRose