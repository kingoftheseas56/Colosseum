#include "server1/policy/EngineSpeedometer.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <utility>

namespace server1::policy {
EngineSpeedometer::EngineSpeedometer(Tick tick, std::uint32_t seconds)
    : tick_(std::move(tick)), size_(4 * (seconds ? seconds : 5)), last_(0)
{
    if(!tick_ || seconds>16383) throw std::invalid_argument("invalid speedometer clock/window");
    last_=static_cast<std::uint16_t>(tick_()-1);
}
double EngineSpeedometer::sample(double delta)
{
    const auto now=tick_();
    auto distance=std::min<std::uint32_t>(static_cast<std::uint16_t>(now-last_),size_);
    last_=now;
    while(distance--){
        if(pointer_==size_)pointer_=0;
        const auto previous=buffer_[pointer_==0 ? size_-1 : pointer_-1];
        if(pointer_>=buffer_.size())buffer_.resize(pointer_+1);
        buffer_[pointer_]=previous;
        ++pointer_;
    }
    // JavaScript's delta && update ignores zero and NaN, but not negatives
    // or infinities. Its non-finite JSON handling belongs to serialization.
    if(delta!=0 && !std::isnan(delta))buffer_[pointer_-1]+=delta;
    const auto top=buffer_[pointer_-1];
    const auto bottom=buffer_.size()<size_ ? 0 : buffer_[pointer_==size_ ? 0 : pointer_];
    return buffer_.size()<4 ? top : 4*(top-bottom)/static_cast<double>(buffer_.size());
}
}
