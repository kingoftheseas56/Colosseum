#pragma once

#include "AccountCredentialStore.h"
#include "AndroidSecureStorageBackend.h"
#include "platform/PlatformKind.h"

#include <memory>

std::unique_ptr<AccountCredentialStore> createAccountCredentialStore(
    Colosseum::Platform::Kind kind = Colosseum::Platform::currentKind(),
    AndroidSecureStorageBackend *androidBackend = nullptr);
