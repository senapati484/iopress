#ifndef EXPRESS_PRO_COMMON_H_
#define EXPRESS_PRO_COMMON_H_

#define EXPRESS_PRO_VERSION "1.0.0"

#if defined(EXPRESS_PRO_LINUX)
#define EXPRESS_PRO_PLATFORM "linux"
#elif defined(EXPRESS_PRO_MACOS)
#define EXPRESS_PRO_PLATFORM "macos"
#elif defined(EXPRESS_PRO_WINDOWS)
#define EXPRESS_PRO_PLATFORM "windows"
#else
#define EXPRESS_PRO_PLATFORM "unknown"
#endif

#include <napi.h>

#include <cstdint>
#include <string>

namespace ExpressPro {
  struct ServerConfig {
    uint16_t port = 8080;
    std::string host = "0.0.0.0";
    int backlog = 511;
  };

}  // namespace ExpressPro

#endif  // EXPRESS_PRO_COMMON_H_
