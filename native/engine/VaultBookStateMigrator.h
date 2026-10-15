#pragma once

// Vault-owned bridge for moving existing Reader 2 path-keyed stores after VaultIdentity reports
// an unambiguous file rename. Reader 2 owns the store shape; this consumes only BookStores' public
// API and never edits Reader 2 sources.

#include <QString>

class VaultBookStateMigrator
{
public:
    // Copies source records only when the destination key is absent. The source remains as a
    // recovery breadcrumb; collisions are a user decision, not a silent merge.
    static bool migrate(const QString& oldPath, const QString& newPath);
};
