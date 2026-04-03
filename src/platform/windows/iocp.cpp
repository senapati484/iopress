#if defined(EXPRESS_PRO_WINDOWS) && defined(EXPRESS_PRO_HAS_IOCP)

#include <napi.h>
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>

namespace maxpress {

namespace Platform {

class IOCPServer {
 public:
  IOCPServer() : iocp_handle_(INVALID_HANDLE_VALUE) {}

  bool Initialize() {
    // Stub: CreateIoCompletionPort would go here
    iocp_handle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    return iocp_handle_ != INVALID_HANDLE_VALUE;
  }

  void Shutdown() {
    if (iocp_handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(iocp_handle_);
      iocp_handle_ = INVALID_HANDLE_VALUE;
    }
  }

 private:
  HANDLE iocp_handle_;
};

}  // namespace Platform

}  // namespace maxpress

#endif  // WINDOWS && IOCP
