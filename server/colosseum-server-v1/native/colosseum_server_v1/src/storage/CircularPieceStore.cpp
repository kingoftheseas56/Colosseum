#include "server1/policy/CircularPieceStore.h"
#include <QCryptographicHash>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace server1::policy {
CircularPieceStore::CircularPieceStore(std::filesystem::path root,CircularStoreMode mode,std::size_t size,std::size_t pieceLength)
 : root_(std::move(root)),mode_(mode),capacity_(pieceLength?size/pieceLength:0),slots_(capacity_)
{ for(auto &s:slots_)s={std::numeric_limits<std::size_t>::max(),std::nullopt,false,false,0,0}; if(mode_==CircularStoreMode::Filesystem&&capacity_)std::filesystem::create_directories(root_/"pieces"); }
std::size_t CircularPieceStore::capacity() const noexcept{return capacity_;}
CircularWriteResult CircularPieceStore::write(std::size_t index,ByteBuffer buffer,const std::set<std::size_t>&selected,const std::set<std::size_t>&locked,std::uint64_t now)
{
 Slot *slot=find(index); if(!slot){auto e=std::find_if(slots_.begin(),slots_.end(),[](const Slot&s){return !s.buffer;});if(e!=slots_.end())slot=&*e;}
 if(!slot){Slot*old=nullptr;for(auto &s:slots_)if(s.buffer&&s.committed&&!selected.count(s.index)&&!locked.count(s.index)&&(!old||old->accessedAt>s.accessedAt))old=&s;if(!old)return{false,std::nullopt,fullError(selected)};const auto reset=old->index;if(old->spilled){std::error_code ignored;std::filesystem::remove(piecePath(reset),ignored);}slot=old;++slot->generation;slot->spilled=false;resetEvents_.push_back(reset);slot->index=index;slot->buffer=std::move(buffer);slot->committed=false;slot->accessedAt=now;return{true,reset,{}};}
 ++slot->generation;slot->index=index;slot->buffer=std::move(buffer);slot->committed=false;slot->spilled=false;slot->accessedAt=now;return{true,std::nullopt,{}};
}
std::optional<ByteBuffer> CircularPieceStore::read(std::size_t index,std::uint64_t now)
{Slot*s=find(index);if(!s||closed_)return std::nullopt;s->accessedAt=now;if(!s->spilled)return s->buffer;std::ifstream in(piecePath(index),std::ios::binary);if(!in)return std::nullopt;return ByteBuffer(std::istreambuf_iterator<char>(in),{});}
CircularCommitResult CircularPieceStore::commit(std::size_t start,std::size_t end,std::string_view expectedSha1)
{
 CircularCommitResult r; r.verification={false,false,start,end+1};
 if(start>end){r.error="invalid commit range";return r;}
 ByteBuffer joined;
 for(std::size_t p=start;p<=end;++p){Slot*s=find(p);if(!s||!s->buffer){r.error="required piece is missing";return r;}if(s->spilled){std::ifstream in(piecePath(p),std::ios::binary);if(!in){r.error="required spilled piece is missing";return r;}joined.insert(joined.end(),std::istreambuf_iterator<char>(in),{});}else joined.insert(joined.end(),s->buffer->begin(),s->buffer->end());}
 r.verification.complete=true;
 const QByteArray input(reinterpret_cast<const char*>(joined.data()),static_cast<qsizetype>(joined.size()));
 const auto actual=QCryptographicHash::hash(input,QCryptographicHash::Sha1).toHex().toStdString();
 if(expectedSha1.empty()||actual!=expectedSha1){r.error="SHA-1 verification failed";return r;}
 r.verification.success=true;r.success=true;r.noNotifyHave=true;
 for(std::size_t p=start;p<=end;++p){Slot*s=find(p);s->committed=true;if(mode_==CircularStoreMode::Filesystem){const auto pending=std::find_if(spills_.begin(),spills_.end(),[&](const auto&entry){return entry.second.piece==p&&entry.second.generation==s->generation&&!entry.second.canceled;});if(pending!=spills_.end())r.spillTokens.push_back(pending->first);else{const auto t=nextToken_++;spills_[t]={p,s->generation,false};r.spillTokens.push_back(t);}}}
 return r;
}
bool CircularPieceStore::cancelSpill(SpillToken t){auto i=spills_.find(t);if(i==spills_.end()||i->second.canceled)return false;i->second.canceled=true;return true;}
bool CircularPieceStore::completeSpill(SpillToken t,bool success){auto i=spills_.find(t);if(i==spills_.end())return false;auto spill=i->second;spills_.erase(i);if(!success||spill.canceled||closed_)return false;Slot*s=find(spill.piece);if(!s||s->generation!=spill.generation||!s->buffer)return false;std::ofstream out(piecePath(spill.piece),std::ios::binary|std::ios::trunc);if(!out)return false;out.write(reinterpret_cast<const char*>(s->buffer->data()),static_cast<std::streamsize>(s->buffer->size()));if(!out)return false;s->buffer=ByteBuffer{'f','s'};s->spilled=true;return true;}
void CircularPieceStore::close(){closed_=true;spills_.clear();slots_.clear();}
const std::vector<std::size_t>&CircularPieceStore::resetEvents()const noexcept{return resetEvents_;}
CircularPieceStore::Slot*CircularPieceStore::find(std::size_t index){auto i=std::find_if(slots_.begin(),slots_.end(),[&](const Slot&s){return s.buffer&&s.index==index;});return i==slots_.end()?nullptr:&*i;}
std::filesystem::path CircularPieceStore::piecePath(std::size_t p)const{return root_/"pieces"/std::to_string(p);}
std::string CircularPieceStore::fullError(const std::set<std::size_t>&selected)const{std::ostringstream m;m<<"circular buf is full, unable to free; unfreeable pieces: ";bool first=true;for(const auto&s:slots_){if(!s.buffer||(s.committed&&!selected.count(s.index)))continue;if(!first)m<<", ";first=false;m<<s.index;if(!s.committed)m<<"uc";}return m.str();}
}
