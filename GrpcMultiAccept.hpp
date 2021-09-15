#pragma once

#include "GrpcServ.hpp"

class GrpcMultiAccept: public GrpcServ::GrpcAccept {
  public:
    GrpcMultiAccept(int thread_count);
    virtual void onAccept(TcpAccept *acc, int fd,
        struct sockaddr *localAddr, struct sockaddr *remoteAddr) override;
    virtual void onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) override;
    virtual void setServ(GrpcServ *serv, Loop *loop) override;
  protected:
    struct Item
    {
      Item(GrpcServ *s, Loop *l): serv(s), loop(l) {}
      GrpcServ *serv;
      Loop *loop;
    };
    int idx_;
    std::vector<Item> servs_;
};

