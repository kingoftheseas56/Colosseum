#pragma once

#include "AccountCredentialStore.h"
#include "AccountSensitiveClipboard.h"
#include "platform/PlatformKind.h"

#include <memory>

class AndroidSecureStorageBackend;

std::unique_ptr<AccountCredentialStore> createAccountCredentialStore(
    Colosseum::Platform::Kind kind = Colosseum::Platform::currentKind(),
    AndroidSecureStorageBackend *androidBackend = nullptr);

std::unique_ptr<AccountSensitiveClipboard> createAccountSensitiveClipboard(
    Colosseum::Platform::Kind kind = Colosseum::Platform::currentKind());
