$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}

function Assert-Absent($hay, $needle, $why) {
    if ($hay -like "*$needle*") { throw "STALE: $needle -- $why" }
}

$header = Get-Content (Join-Path $root "native/engine/MangaDownloader.h") -Raw
$source = Get-Content (Join-Path $root "native/engine/MangaDownloader.cpp") -Raw
$resolver = Get-Content (Join-Path $root "native/engine/MangaImageHostResolver.cpp") -Raw

# A first-use image request must never synchronously resolve a CDN host on the
# application thread. Requests sharing an unresolved host must join one lookup,
# then resume with the same retry attempt once that lookup completes.
Assert-Absent ($source + $resolver) 'QHostInfo::fromName' "Manga image fetches must not block the UI thread in DNS"
Assert-Contains $resolver 'QHostInfo::lookupHost' "first-use CDN resolution must be asynchronous"
Assert-Contains $source 'm_hostResolver.resolve(' "MangaDownloader must use the injectable async resolver seam"
Assert-Contains $resolver 'm_lifetime->resolver = nullptr' "late injected resolver completion must be lifetime-gated"
Assert-Contains $resolver 'm_lookupGenerations' "cancelled host lookups need generation fencing"
Assert-Contains $source 'm_pinLookupRequests' "MangaDownloader must retain resolver request ids for cancellation"
Assert-Contains $source 'm_pinTried.remove(host)' "cancelling the last host request must permit a fresh lookup"
Assert-Contains $source 'scraperPending' "scraper lifetime must be explicit in the download Job"
Assert-Contains $source 'std::make_shared<JobLifetime>()' "scraper callbacks need a shared Job lifetime gate"
Assert-Contains $source 'lifetime->job = nullptr' "Job lifetime must be invalidated before deletion"
Assert-Contains $source 'if (!job || job->cancelled) return' "late scraper signals must stop after cancellation"
Assert-Absent $source '[this, job](const QList<PageInfo>& pages)' "scraper pagesReady must not capture a raw Job directly"
Assert-Absent $source '[this, job](const QString& e)' "scraper errorOccurred must not capture a raw Job directly"
Assert-Contains $header 'm_pendingPinRequests' "unresolved-host page requests need a coalescing queue"
Assert-Contains $header 'm_pinLookupInFlight' "one lookup must be tracked per unresolved host"
Assert-Contains $source 'm_pinLookupInFlight.insert(host)' "the first request must own the host lookup"
Assert-Contains $source 'm_pinLookupInFlight.contains(host)' "later pages must join an existing host lookup"
Assert-Contains $source 'm_pinLookupInFlight.remove(host)' "lookup completion must release the coalescing gate"
Assert-Contains $source 'm_pendingPinRequests.take(host)' "all queued pages must resume from one lookup callback"
Assert-Contains $source 'fetchImage(request.job, request.pageIndex, request.attempt)' "DNS completion must not consume image retries"
Assert-Contains $source 'job->cancelled' "pending DNS work must be cancellation-safe"
Assert-Contains $source 'removePendingImageRequests(job)' "cancellation and cleanup must release queued page slots"
Assert-Contains $source 'req.setPeerVerifyName(host)' "IPv4 pinning must preserve TLS hostname verification"
Assert-Contains $source 'Http2AllowedAttribute, false' "IPv4 pinning must preserve the HTTP/2 workaround"
Assert-Contains $source 'req.setRawHeader("Host", host.toUtf8())' "IPv4 pinning must preserve the CDN Host header"

Write-Host "manga async DNS contract OK"
