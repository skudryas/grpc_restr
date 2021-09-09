#pragma once

#include "GrpcServ.hpp"

class GrpcMultiAccept: public GrpcServ::GrpcAccept {
  public:
    GrpcMultiAccept(GrpcServ& serv, int thread_num = 1);
    virtual void onAccept(TcpAccept *acc, int fd,
        struct sockaddr *localAddr, struct sockaddr *remoteAddr) override;
    virtual void onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) override;
  protected:
    int threadNum_;
};

