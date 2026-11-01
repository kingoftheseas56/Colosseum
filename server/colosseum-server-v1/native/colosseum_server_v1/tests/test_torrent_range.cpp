#include "server1/http/TorrentRange.h"
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char **argv){
 if(argc==2 && std::string(argv[1])=="--probe"){std::string line;while(std::getline(std::cin,line)){auto tab=line.find("\t");auto r=server1::http::firstTorrentRange(std::stoull(line.substr(0,tab)),line.substr(tab+1));if(r)std::cout<<r->start<<","<<r->end<<"\n";else std::cout<<"null\n";}return 0;}
 struct Case {const char *header;long start,end;};
 const Case cases[]={
  {"bytes=0-0",0,0},{"bytes=2-",2,15},{"bytes=-3",13,15},
  {"bytes=100-",-1,-1},{"bytes=0-100",0,15},{"bytes=-100",-1,-1},
  {"nonsense",-1,-1},{"bytes=",-1,-1},{"bytes=fish",-1,-1},
  {"bytes=8-2",-1,-1},{"bytes=99-100,4-6",4,6},{"bytes=0-0,5-9",0,0},
  {"items=2-4",2,4},{"=2-4",2,4},{"bytes= 2 - 4 ",2,4},
  {"bytes=2junk-4junk",2,4},{"bytes=2.5-4.8",2,4},{"bytes=+2-+4",2,4},
  {"bytes=0x2-0x4",0,0},{"bytes=-0",-1,-1},{"bytes=15-",15,15},
  {"bytes=16-",-1,-1},{"bytes=2-3-8",2,3},{"bytes=--3",-1,-1}};
 try{for(const auto &c:cases){const auto r=server1::http::firstTorrentRange(16,c.header);if(c.start<0){if(r)throw std::runtime_error(c.header);}else if(!r || r->start!=static_cast<std::size_t>(c.start) || r->end!=static_cast<std::size_t>(c.end))throw std::runtime_error(c.header);}
 if(server1::http::firstTorrentRange(0,"bytes=0-"))throw std::runtime_error("empty file");
 std::cout<<"M176 first-range cases passed\n";
 }catch(const std::exception &e){std::cerr<<"range mismatch: "<<e.what()<<'\n';return 1;}
}
