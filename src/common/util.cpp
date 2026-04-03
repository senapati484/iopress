#include "../include/common.h"
#include <napi.h>

namespace iopress {

namespace Util {

Napi::Value ParseConfig(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  return Napi::Object::New(env);
}

}  // namespace Util

}  // namespace iopress
