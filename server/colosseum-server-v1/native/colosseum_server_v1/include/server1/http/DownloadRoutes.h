#pragma once
#include <QString>
namespace server1::downloads {class DownloadManager;}
namespace server1::http {
// The caller owns manager and must keep it alive through router retirement.
void mountDownloadRoutes(void* router,downloads::DownloadManager* manager,const QString& ipcKey);
}
