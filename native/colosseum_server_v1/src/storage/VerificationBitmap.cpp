#include "server1/policy/PieceStore.h"

#include <fstream>
#include <stdexcept>

namespace server1::policy {

VerificationBitmap::VerificationBitmap(std::size_t count, std::filesystem::path persistPath)
    : bits_(count, false)
    , persistPath_(std::move(persistPath))
{
    if (persistPath_.empty() || !std::filesystem::exists(persistPath_))
        return;
    std::ifstream input(persistPath_, std::ios::binary);
    for (std::size_t index = 0; index < bits_.size(); ++index) {
        char value = 0;
        if (!input.get(value))
            break;
        bits_[index] = value != 0;
    }
}

bool VerificationBitmap::get(std::size_t index) const
{
    if (index >= bits_.size())
        throw std::out_of_range("verification bitmap index");
    return bits_[index];
}

void VerificationBitmap::set(std::size_t index, bool value)
{
    if (index >= bits_.size())
        throw std::out_of_range("verification bitmap index");
    bits_[index] = value;
}

void VerificationBitmap::persist() const
{
    if (persistPath_.empty())
        return;
    if (!persistPath_.parent_path().empty())
        std::filesystem::create_directories(persistPath_.parent_path());
    std::ofstream output(persistPath_, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
        throw std::runtime_error("verification bitmap open failed: " + persistPath_.string());
    for (const bool bit : bits_)
        output.put(bit ? '\1' : '\0');
    if (!output)
        throw std::runtime_error("verification bitmap write failed: " + persistPath_.string());
    output.flush();
    if (!output)
        throw std::runtime_error("verification bitmap flush failed: " + persistPath_.string());
}

void VerificationBitmap::invalidateMissing(const std::vector<bool> &filePresent)
{
    const auto count = std::min(bits_.size(), filePresent.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (!filePresent[index])
            bits_[index] = false;
    }
}

} // namespace server1::policy
