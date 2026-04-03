#ifndef EXPRESS_PRO_SERVER_H_
#define EXPRESS_PRO_SERVER_H_

#include <napi.h>

#include "common.h"

namespace maxpress {
  class Server {
  public:
    static const char* GetBackendName();
    static Napi::Value CreateServer(const Napi::CallbackInfo& info);
  };

}  // namespace maxpress

#endif  // EXPRESS_PRO_SERVER_H_
