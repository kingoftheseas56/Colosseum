#pragma once

#include "PlatformAccountCredentialStore.h"

// Source compatibility for existing callers. Runtime selection is platform-specific.
using WindowsAccountCredentialStore = PlatformAccountCredentialStore;
