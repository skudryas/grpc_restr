#include "GrpcMultiAccept.hpp"

GrpcMultiAccept::GrpcMultiAccept(): idx_(0)
{
}

void GrpcMultiAccept::onAccept(TcpAccept *acc, int fd,
        struct sockaddr *localAddr, struct sockaddr *remoteAddr)
{
  if (servs_.empty()) {
    LOG(ERROR) << "Got connection, but no server!";
    return;
  }
  int idx = (idx_ % servs_.size());
  idx_ = (idx + 1) % servs_.size();
  auto &serv = servs_[idx]; // Round-robin
  auto conn = std::make_unique<Http2Conn>(serv.loop, serv.serv, fd, remoteAddr, localAddr);
  serv.serv->gotConn(std::move(conn));
}

void GrpcMultiAccept::onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) {
  DLOG(ERROR) << "Grpc server error: " << error << " code: "
            << strerror(code);
  for (auto& serv: servs_) // XXX
    serv.serv->gotError(acc, error, code);
}

void GrpcMultiAccept::setServ(GrpcServ *serv, Loop *loop)
{
  assert(serv != nullptr && loop != nullptr);
  servs_.emplace_back(serv, loop);
}
