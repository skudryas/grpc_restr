#include "GrpcRestr.hpp"
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
  l.run();
}
