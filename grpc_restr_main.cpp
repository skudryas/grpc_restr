#include "GrpcRestr.hpp"
#include <list>
#include <thread>
#include <unistd.h>

int main(int argc, char **argv)
{
  set_log_level(argc, argv);
  const char *host = (argc - optind > 0 ? argv[optind] : "0.0.0.0"),
             *port = (argc - optind > 1 ? argv[optind + 1] : "8002");
  Loop l;
  Repl::Repl repl;
  auto prov = std::make_unique<GrpcRestrProvider>(repl);
  GrpcServ gserv(prov.get());
  TcpAccept a(&l, gserv.acceptDlgt(), host, port, 10, 0);
  auto f = [&l] (size_t repl_tid) {
    Repl::g_repl_tid = repl_tid;
    l.run();
  };
  std::list<std::thread> tp;
  size_t repl_tid = 0;
  for (int i = 0; i < SERV_THREAD_NUM; ++i) {
    tp.emplace_back(f, repl_tid++);
  }
  f(repl_tid);
  //l.run();
}
