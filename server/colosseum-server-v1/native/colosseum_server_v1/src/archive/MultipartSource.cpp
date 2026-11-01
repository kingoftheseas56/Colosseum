#include "server1/archive/ArchiveSources.h"
#include <limits>
namespace server1::archive {
qint64 multipartLength(const QList<qint64>&lengths){qint64 n=0;for(auto l:lengths){if(l<1)continue;if(n>std::numeric_limits<qint64>::max()-(l-1))return -1;n+=l-1;}return n;}
QList<PartRange> multipartRanges(const QList<qint64>&lengths,qint64 start,qint64 end,QString*error){
 if(error)error->clear();auto fail=[&](){if(error)*error="Illegal start/end offset";return QList<PartRange>{};};auto total=multipartLength(lengths);if(lengths.isEmpty()||total<0||start<0||end>=total||end<start)return fail();
 struct Map{qint64 start,end;};QList<Map> maps;qint64 offset=0;for(qint64 l:lengths){if(l<1||offset>std::numeric_limits<qint64>::max()-l)return fail();maps.append({offset,offset+l-1});offset+=l;}
 auto find=[&](qint64 p){for(int i=0;i<maps.size();++i)if(p>=maps[i].start&&p<=maps[i].end)return i;return 0;};int first=find(start),last=find(end);QList<PartRange> out;for(int i=first;i<=last;++i)out.append({i,0,lengths[i]-1});if(out.isEmpty())return fail();out.front().start+=qAbs(maps[first].start-start);qint64 diff=qAbs(maps[last].end-end);if(diff==lengths.size())diff=0;out.back().end-=diff;return out;
}
}
