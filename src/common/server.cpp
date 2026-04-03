#include "../include/server.h"
#include <napi.h>

namespace maxpress {

const char* Server::GetBackendName() {
#if defined(EXPRESS_PRO_HAS_IOURING)
  return "io_uring";
#elif defined(EXPRESS_PRO_HAS_KQUEUE)
  return "kqueue";
#elif defined(EXPRESS_PRO_HAS_IOCP)
  return "iocp";
#else
  return "libuv";
#endif
}

Napi::Value Server::CreateServer(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  // Stub implementation - returns a simple object
  Napi::Object server = Napi::Object::New(env);

  server.Set(Napi::String::New(env, "port"), Napi::Number::New(env, 8080));
  server.Set(Napi::String::New(env, "running"), Napi::Boolean::New(env, false));
  server.Set(Napi::String::New(env, "backend"), Napi::String::New(env, GetBackendName()));

  return server;
}

}  // namespace maxpress
