#if defined(EXPRESS_PRO_LINUX) && !defined(EXPRESS_PRO_HAS_IOURING)

#include <napi.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace iopress {

namespace Platform {

class EpollServer {
 public:
  EpollServer() : epoll_fd_(-1) {}

  bool Initialize() {
    // Stub: epoll_create1 would go here
    epoll_fd_ = 0;  // placeholder
    return true;
  }

  void Shutdown() {
    if (epoll_fd_ >= 0) {
      // close(epoll_fd_);
      epoll_fd_ = -1;
    }
  }

 private:
  int epoll_fd_;
};

}  // namespace Platform

}  // namespace iopress

#endif  // LINUX && !IO_URING
