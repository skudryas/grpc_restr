#include "GrpcRestr.hpp"
#include "GrpcMultiAccept.hpp"
#include <list>
#include <thread>
#include <unistd.h>

int main(int argc, char **argv)
{
  set_log_level(argc, argv);
  const char *host = (argc - optind > 0 ? argv[optind] : "0.0.0.0"),
             *port = (argc - optind > 1 ? argv[optind + 1] : "8002");
  Repl::GrpcRepl<mbproto::ConsumeRequest> repl(SERV_THREAD_NUM);
  auto prov = std::make_unique<GrpcRestrProvider>(repl);
  GrpcServ gserv(prov.get());
  auto acceptDefault = std::make_unique<GrpcServ::GrpcAccept>(gserv);
  auto acceptMulti = std::make_unique<GrpcMultiAccept>(gserv, 2);
  gserv.setAcceptDlgt(std::move(acceptMulti));

  auto f = [&repl] (size_t repl_tid) {
    Repl::g_repl_tid = repl_tid;
    repl.loop();
    //l.run();
  };
  std::list<std::thread> tp;
  size_t repl_tid = 0;
  for (int i = 0; i < SERV_THREAD_NUM; ++i) {
    tp.emplace_back(f, repl_tid++);
  }
  Repl::g_repl_tid = repl_tid;
  auto g = [host, port, &gserv] () {
    Loop l;
    TcpAccept a(&l, gserv.acceptDlgt(), host, port, 1, 0);
    l.run();
  };

/*  for (int i = 0; i < SERV_THREAD_NUM; ++i) {
    std::cout << "here" << std::endl;
    tp.emplace_back(g);
  }*/
  g();
}
