#pragma once

#include "server1/policy/FileReader.h"
#include <memory>

namespace server1::http {
// Serialized owner-lane adapter. Retains dependencies through response retirement,
// and closes the reader on socket drain, cancellation, or stream replacement.
// The caller supplies the reader's engine/store owner in dependencies.
// On failure the response and reader remain unchanged.
bool attachFileReader(void *response, std::shared_ptr<policy::FileReader> reader,
                      std::shared_ptr<void> dependencies);
}
