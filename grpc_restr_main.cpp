#include "GrpcRestr.hpp"
#include "GrpcMultiAccept.hpp"
#include <list>
#include <thread>
#include <unistd.h>

void main_multi_loop(const char *host, const char *port);
void main_single_loop(const char *host, const char *port);

int main(int argc, char **argv)
{
  set_log_level(argc, argv);
  const char *host = (argc - optind > 0 ? argv[optind] : "0.0.0.0"),
             *port = (argc - optind > 1 ? argv[optind + 1] : "8002");

//  main_multi_loop(host, port);
  main_single_loop(host, port);
}

void main_multi_loop(const char *host, const char *port)
{
  Repl::GrpcRepl<mbproto::ConsumeRequest> repl(SERV_THREAD_NUM);
#ifdef USE_MULTI_ACCEPT
  std::shared_ptr<GrpcServ::GrpcAccept> acceptor = std::make_shared<GrpcMultiAccept>(SERV_THREAD_NUM);

  auto grpc_thread = [&repl, &acceptor] (size_t repl_tid) {
    auto prov = std::make_unique<GrpcRestrProvider>(repl);
    GrpcServ gserv(prov.get());
    Loop l;
    acceptor->setServ(&gserv, &l);
    gserv.setAcceptDlgt(acceptor);
    Repl::g_repl_tid = repl_tid;
    l.run();
  };

  std::list<std::thread> grpc_tp;
  size_t repl_tid = 0;
  for (int i = 0; i < SERV_THREAD_NUM; ++i) {
    grpc_tp.emplace_back(grpc_thread, repl_tid++);
  }

#else
  auto prov = std::make_unique<GrpcRestrProvider>(repl);
  GrpcServ gserv(prov.get());
  auto acceptor = std::make_shared<GrpcServ::GrpcAccept>();
  acceptor->setServ(&gserv, nullptr);
  gserv.setAcceptDlgt(acceptor);

  auto repl_thread = [&repl] (size_t repl_tid) {
    Repl::g_repl_tid = repl_tid;
    repl.loop();
  };
  std::list<std::thread> repl_tp;
  size_t repl_tid = 0;
  for (int i = 0; i < SERV_THREAD_NUM; ++i) {
    repl_tp.emplace_back(repl_thread, repl_tid++);
  }
#endif

  auto listen_thread = [host, port, &acceptor] () {
    Loop l;
    TcpAccept a(&l, acceptor.get(), host, port, 1, 0);
    l.run();
  };
  listen_thread();
}

void main_single_loop(const char *host, const char *port)
{
  Repl::GrpcRepl<mbproto::ConsumeRequest> repl(SERV_THREAD_NUM);

  auto prov = std::make_unique<GrpcRestrProvider>(repl);
  GrpcServ gserv(prov.get());
  auto acceptor = std::make_shared<GrpcServ::GrpcAccept>();
  acceptor->setServ(&gserv, nullptr);
  gserv.setAcceptDlgt(acceptor);

  Loop l;
  TcpAccept a(&l, acceptor.get(), host, port, 1, 0);

  auto main_thread = [&l] (size_t repl_tid) {
    Repl::g_repl_tid = repl_tid;
    l.run();
  };

  std::list<std::thread> tp;
  size_t repl_tid = 0;
  for (; repl_tid < SERV_THREAD_NUM - 1; ++repl_tid) {
    tp.emplace_back(main_thread, repl_tid);
  }
  main_thread(repl_tid);
}
