#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpSocket>
#include <iostream>
#include <stdexcept>
struct Root {void *server=nullptr;int retired=0,closed=0,completed=0;};
struct Heap {Root *root;int marker=123;};
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);Root root;
 void *router=server1_http_router_create();
 auto handler=+[](const void *,void *response,server1_http_next_fn,void *,void *opaque){
  auto *heap=new Heap{static_cast<Root *>(opaque)};
  server1_http_response_set_status(response,200);
  server1_http_response_set_lifetime(response,+[](void *p){++static_cast<Root *>(p)->retired;},opaque);
  return server1_http_response_set_stream(response,+[](void *p,void *,std::size_t)->std::ptrdiff_t{
   auto *heap=static_cast<Heap *>(p);Root *root=heap->root;
   void *server=root->server;root->server=nullptr;server1_http_server_destroy(server);
   if(heap->marker!=123 || root->retired || root->closed)throw std::runtime_error("active read ownership retired early");
   ++root->completed;return 1;
  },+[](void *p){auto *heap=static_cast<Heap *>(p);++heap->root->closed;delete heap;},heap,1);
 };
 try{
  if(!server1_http_router_add_handler(router,0,0,"GET","/read",handler,&root,nullptr))return 2;
  root.server=server1_http_server_create(router,65536);if(!server1_http_server_listen(root.server,0))return 3;
  QTcpSocket client;client.connectToHost("127.0.0.1",server1_http_server_port(root.server));if(!client.waitForConnected(1000))return 4;
  client.write("GET /read HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");client.flush();
  QElapsedTimer timer;timer.start();while(timer.elapsed()<2000 && !root.completed)QCoreApplication::processEvents(QEventLoop::AllEvents,10);
  if(root.completed!=1 || root.closed!=1 || root.retired!=1 || root.server)return 5;
  server1_http_router_destroy(router);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  std::cout<<"PASS active read -> stream context close -> response lifetime retirement\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
