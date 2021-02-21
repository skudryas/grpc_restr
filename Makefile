COMMON_CC_FLAGS = -Werror -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -fno-omit-frame-pointer -DDEBUGLOG -fsanitize=address
COMMON_LD_FLAGS = -lasan -pthread

BASE_OBJS = TcpAccept.o TcpConn.o Loop.o Chain.o Log.o Timer.o Async.o
GRPC_OBJS = Http2Conn.o GrpcServ.o

GRPC_DEFAULT_OBJS = GrpcDefault.o default.pb.o
GRPC_DEFAULT_PBS = default.pb.cc default.pb.h

GRPC_RTKMB_OBJS = GrpcRestr.o  Match.o Repl.o rtkmb.pb.o
GRPC_RTKMB_PBS = rtkmb.pb.cc rtkmb.pb.h

GRPC_SERVER_OBJS = test_grpc_server.o

GRPC_RESTR_OBJS = grpc_restr_main.o

GRPC_LIBS = -lnghttp2 `pkg-config --libs protobuf`
ECHO_CLIENT_OBJS = tcp_echo_client.o
ECHO_SERVER_OBJS = tcp_echo_server.o

all: echo_client echo_server grpc_server grpc_restr

%.o: %.cpp
	g++ $< -c -g -std=c++17 $(COMMON_CC_FLAGS)

%.o: %.cc
	g++ $< -c -g -std=c++17 $(COMMON_CC_FLAGS)

$(GRPC_SERVER_OBJS): $(GRPC_DEFAULT_PBS)

GrpcDefault.o: GrpcDefault.hpp GrpcDefault.cpp $(GRPC_DEFAULT_PBS)

default.pb.h: default.pb.cc

default.pb.cc:
	protoc --cpp_out=. default.proto

$(GRPC_RESTR_OBJS): $(GRPC_RTKMB_PBS)

GrpcRestr.o: GrpcRestr.hpp GrpcRestr.cpp $(GRPC_RTKMB_PBS)

rtkmb.pb.h: rtkmb.pb.cc

rtkmb.pb.cc:
	protoc --cpp_out=. rtkmb.proto

clean:
	rm -f echo_client echo_server grpc_server grpc_restr $(ECHO_CLIENT_OBJS) $(ECHO_SERVER_OBJS) $(BASE_OBJS) $(GRPC_OBJS) $(GRPC_SERVER_OBJS) $(GRPC_DEFAULT_PBS) $(GRPC_DEFAULT_OBJS) $(GRPC_RTKMB_PBS) $(GRPC_RTKMB_OBJS)

echo_client: $(ECHO_CLIENT_OBJS) $(BASE_OBJS)
	g++ -o echo_client $(ECHO_CLIENT_OBJS) $(BASE_OBJS) $(COMMON_LD_FLAGS)

echo_server: $(ECHO_SERVER_OBJS) $(BASE_OBJS)
	g++ -o echo_server $(ECHO_SERVER_OBJS) $(BASE_OBJS) $(COMMON_LD_FLAGS)

grpc_server: $(GRPC_SERVER_OBJS) $(BASE_OBJS) $(GRPC_OBJS) $(GRPC_DEFAULT_OBJS)
	g++ -o grpc_server $(GRPC_SERVER_OBJS) $(BASE_OBJS) $(GRPC_OBJS) $(GRPC_DEFAULT_OBJS) $(COMMON_LD_FLAGS) $(GRPC_LIBS)

grpc_restr: $(GRPC_RESTR_OBJS) $(BASE_OBJS) $(GRPC_OBJS) $(GRPC_DEFAULT_OBJS) $(GRPC_RTKMB_OBJS)
	g++ -o grpc_restr $(GRPC_RESTR_OBJS) $(BASE_OBJS) $(GRPC_OBJS) $(GRPC_DEFAULT_OBJS) $(GRPC_RTKMB_OBJS) $(COMMON_LD_FLAGS) $(GRPC_LIBS)


