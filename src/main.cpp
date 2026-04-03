#include <napi.h>
#include "include/server.h"

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set(
      Napi::String::New(env, "version"),
      Napi::String::New(env, EXPRESS_PRO_VERSION));

  exports.Set(
      Napi::String::New(env, "platform"),
      Napi::String::New(env, EXPRESS_PRO_PLATFORM));

  exports.Set(
      Napi::String::New(env, "backend"),
      Napi::String::New(env, maxpress::Server::GetBackendName()));

  exports.Set(
      Napi::String::New(env, "createServer"),
      Napi::Function::New(env, maxpress::Server::CreateServer));

  return exports;
}

NODE_API_MODULE(express_pro_native, Init)
