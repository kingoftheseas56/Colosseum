#include "server1/http/SafeFileRegex.h"
extern "C" {
#include "libregexp.h"
}
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>
#include <stdexcept>
#ifdef _MSC_VER
#include <intrin.h>
#endif
namespace {
struct Heap {
    std::size_t bytes=0;
    std::uintptr_t stackBase=0;
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point::max();
    bool interrupted=false;
};
struct alignas(std::max_align_t) Allocation {std::size_t bytes;};
constexpr std::size_t heapLimit=64U*1024U*1024U;
std::uintptr_t stackAddress(){
    // ASan can put local variables on a fake stack, so measure the real frame.
#ifdef _MSC_VER
    return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
#else
    return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
#endif
}
}
extern "C" void *lre_realloc(void *opaque,void *pointer,std::size_t size){
    auto &heap=*static_cast<Heap *>(opaque);
    auto *old=pointer?static_cast<Allocation *>(pointer)-1:nullptr;
    const auto previous=old?old->bytes:0;
    if(!size){heap.bytes-=previous;std::free(old);return nullptr;}
    if(size>heapLimit || heap.bytes-previous>heapLimit-size)return nullptr;
    auto *next=static_cast<Allocation *>(std::realloc(old,sizeof(Allocation)+size));
    if(!next)return nullptr;
    next->bytes=size;heap.bytes=heap.bytes-previous+size;return next+1;
}
extern "C" int lre_check_stack_overflow(void *opaque,std::size_t extra){
    auto &heap=*static_cast<Heap *>(opaque);
    const auto current=stackAddress();
    const auto depth=current>heap.stackBase?current-heap.stackBase:heap.stackBase-current;
    return depth>512U*1024U || extra>512U*1024U-depth;
}
extern "C" int lre_check_timeout(void *opaque){
    auto &heap=*static_cast<Heap *>(opaque);
    if(std::chrono::steady_clock::now()>=heap.deadline)heap.interrupted=true;
    return heap.interrupted;
}
namespace server1::http {
FileRegexResult matchFilePattern(const QString &subject,const QString &expression){
    Heap heap;heap.stackBase=stackAddress();
    struct Cleanup {Heap *heap;void operator()(std::uint8_t *p)const{lre_realloc(heap,p,0);}};
    const Cleanup cleanup{&heap};
    using Bytecode=std::unique_ptr<std::uint8_t,Cleanup>;
    char error[256]={};
    auto compile=[&](const QString &pattern,int flags){
        // libregexp expects CESU-8 in legacy mode, UTF-8 (preserving lone
        // surrogates) with u/v. QString::toUtf8 would replace lone surrogates.
        QByteArray bytes;
        for(qsizetype i=0;i<pattern.size();++i){
            std::uint32_t c=pattern.at(i).unicode();
            if((flags&(LRE_FLAG_UNICODE|LRE_FLAG_UNICODE_SETS)) && pattern.at(i).isHighSurrogate() && i+1<pattern.size() && pattern.at(i+1).isLowSurrogate()){
                c=QChar::surrogateToUcs4(pattern.at(i),pattern.at(i+1));++i;
            }
            if(c<0x80)bytes.append(static_cast<char>(c));
            else if(c<0x800){bytes.append(static_cast<char>(0xc0|(c>>6)));bytes.append(static_cast<char>(0x80|(c&63)));}
            else if(c<0x10000){bytes.append(static_cast<char>(0xe0|(c>>12)));bytes.append(static_cast<char>(0x80|((c>>6)&63)));bytes.append(static_cast<char>(0x80|(c&63)));}
            else{bytes.append(static_cast<char>(0xf0|(c>>18)));bytes.append(static_cast<char>(0x80|((c>>12)&63)));bytes.append(static_cast<char>(0x80|((c>>6)&63)));bytes.append(static_cast<char>(0x80|(c&63)));}
        }
        int length=0;
        return Bytecode(lre_compile(&length,error,sizeof(error),bytes.constData(),static_cast<std::size_t>(bytes.size()),flags,&heap),cleanup);
    };
    Bytecode bytecode(nullptr,cleanup);
    const auto slash=expression.lastIndexOf('/');
    const bool lineBreak=expression.contains('\n') || expression.contains('\r') || expression.contains(QChar(0x2028)) || expression.contains(QChar(0x2029));
    if(expression.startsWith('/') && slash>0 && !lineBreak){
        const auto flags=expression.mid(slash+1);int bits=0;bool valid=true;
        for(const auto flag:flags){
            int bit=0;
            switch(flag.unicode()){
            case 'g':bit=LRE_FLAG_GLOBAL;break;case 'i':bit=LRE_FLAG_IGNORECASE;break;
            case 'm':bit=LRE_FLAG_MULTILINE;break;case 's':bit=LRE_FLAG_DOTALL;break;
            case 'u':bit=LRE_FLAG_UNICODE;break;case 'y':bit=LRE_FLAG_STICKY;break;
            case 'd':bit=LRE_FLAG_INDICES;break;case 'v':bit=LRE_FLAG_UNICODE_SETS;break;
            default:valid=false;break;
            }
            if(bits&bit)valid=false;bits|=bit;
        }
        if((bits&LRE_FLAG_UNICODE) && (bits&LRE_FLAG_UNICODE_SETS))valid=false;
        if(valid)bytecode=compile(expression.mid(1,slash-1),bits);
    }
    // Invalid /pattern/flags remain a string in M172 and are compiled literally.
    if(!bytecode)bytecode=compile(expression,0);
    if(!bytecode)return {false,std::nullopt,QString::fromUtf8(error)};
    const auto flags=lre_get_flags(bytecode.get());
    std::vector<std::uint8_t *> captures(static_cast<std::size_t>(lre_get_capture_count(bytecode.get()))*2);
    const auto *text=reinterpret_cast<const std::uint8_t *>(subject.utf16());
    heap.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
    int start=0;bool found=false;
    for(;;){
        if(lre_check_timeout(&heap))return {true,std::nullopt,{}};
        const auto result=lre_exec(captures.data(),bytecode.get(),text,start,static_cast<int>(subject.size()),1,&heap);
        if(result<0)return {true,std::nullopt,{}};
        if(!result)return {true,found,{}};
        found=true;
        if(!(flags&LRE_FLAG_GLOBAL))return {true,true,{}};
        const auto begin=static_cast<int>((captures[0]-text)/2);
        start=static_cast<int>((captures[1]-text)/2);
        if(start==begin){
            if(start>=subject.size())return {true,true,{}};
            if((flags&(LRE_FLAG_UNICODE|LRE_FLAG_UNICODE_SETS)) && subject.at(start).isHighSurrogate() && start+1<subject.size() && subject.at(start+1).isLowSurrogate())++start;
            ++start;
        }
    }
}
std::optional<std::size_t> selectFilePattern(const QStringList &names,const QStringList &patterns){
    std::optional<std::size_t> selected;
    for(qsizetype i=0;i<names.size();++i){
        for(const auto &pattern:patterns){
            const auto result=matchFilePattern(names[i],pattern);
            if(!result.valid)throw std::invalid_argument("invalid file selection regex");
            if(result.matches && *result.matches){
                selected=static_cast<std::size_t>(i);
                // Array.find returns the original element. Empty strings match
                // but leave the outer find false, so it visits the next file.
                if(!pattern.isEmpty())return selected;
                break;
            }
        }
    }
    return selected;
}

}
