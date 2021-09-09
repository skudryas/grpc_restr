#include "GrpcMultiAccept.hpp"

GrpcMultiAccept::GrpcMultiAccept(GrpcServ& serv, int thread_num):
  GrpcServ::GrpcAccept(serv), threadNum_(thread_num)
{
}

void GrpcMultiAccept::onAccept(TcpAccept *acc, int fd,
        struct sockaddr *localAddr, struct sockaddr *remoteAddr)
{
  auto conn = std::make_unique<Http2Conn>(acc->loop(), &serv_, fd, remoteAddr, localAddr);
  serv_.gotConn(std::move(conn));
}

void GrpcMultiAccept::onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) {
  DLOG(ERROR) << "Grpc server error: " << error << " code: "
            << strerror(code);
  serv_.gotError(acc, error, code);
}


