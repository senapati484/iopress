#ifndef EXPRESS_PRO_SERVER_H_
#define EXPRESS_PRO_SERVER_H_

#include <napi.h>

#include "common.h"

namespace norvex {
  class Server {
  public:
    static const char* GetBackendName();
    static Napi::Value CreateServer(const Napi::CallbackInfo& info);
  };

}  // namespace norvex

#endif  // EXPRESS_PRO_SERVER_H_
