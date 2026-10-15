.pragma library

var MONTH_NAMES = [
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
]

// C++ QVariantList values nested inside a QVariantMap reach QML as indexed,
// length-bearing sequence objects. They are iterable like arrays, but
// Array.isArray() is false, so strict array guards silently discarded the
// native ActivityStore's highlights and recent-activity rows.
function sequenceValues(value) {
    if (Array.isArray(value))
        return value
    if (!value || typeof value === "string" || typeof value.length !== "number")
        return []
    var result = []
    for (var i = 0; i < value.length; ++i)
        result.push(value[i])
    return result
}

function validMonthKey(value) {
    return typeof value === "string" && /^\d{4}-(0[1-9]|1[0-2])$/.test(value)
}

function monthKeys(earliest, current) {
    if (!validMonthKey(current))
        return []
    var first = validMonthKey(earliest) && earliest <= current ? earliest : current
    var year = Number(first.slice(0, 4))
    var month = Number(first.slice(5, 7))
    var result = []
    for (var guard = 0; guard < 1200; ++guard) {
        var key = year + "-" + (month < 10 ? "0" + month : String(month))
        result.push(key)
        if (key === current)
            break
        month += 1
        if (month === 13) {
            month = 1
            year += 1
        }
    }
    return result
}

function currentMonthKey() {
    var now = new Date()
    var month = now.getMonth() + 1
    return now.getFullYear() + "-" + (month < 10 ? "0" + month : String(month))
}

function collectProjections(store, earliest, current) {
    if (!store)
        return []
    void store.revision
    var keys = monthKeys(earliest, current)
    var result = []
    for (var i = 0; i < keys.length; ++i) {
        var projection = store.projectMonth(keys[i])
        if (projection && projection.month)
            result.push(projection)
    }
    return result
}

function projectionForMonth(projections, monthKey) {
    var source = Array.isArray(projections) ? projections : []
    for (var i = 0; i < source.length; ++i)
        if (String(source[i].month || "") === monthKey)
            return source[i]
    return ({ month: monthKey, watchSeconds: 0, listenSeconds: 0, pagesRead: 0,
              completedCount: 0, activeDays: 0, highlights: [], recentActivity: [] })
}

function formatCount(value) {
    var number = Math.max(0, Math.round(Number(value) || 0))
    return String(number).replace(/\B(?=(\d{3})+(?!\d))/g, ",")
}

function formatDuration(seconds) {
    var totalMinutes = Math.floor(Math.max(0, Number(seconds) || 0) / 60)
    var hours = Math.floor(totalMinutes / 60)
    var minutes = totalMinutes % 60
    return hours > 0 ? (hours + "h " + minutes + "m") : (minutes + "m")
}

function formatPages(value) {
    var count = Math.max(0, Math.round(Number(value) || 0))
    return formatCount(count) + (count === 1 ? " page" : " pages")
}

function worldName(world) {
    if (world === "theatre") return "Theatre"
    if (world === "tankoban") return "Tankoban"
    if (world === "biblio") return "Biblio"
    if (world === "vault") return "Vault"
    return "All worlds"
}

function monthLabel(key) {
    if (!validMonthKey(key))
        return ""
    return MONTH_NAMES[Number(key.slice(5, 7)) - 1] + " " + key.slice(0, 4)
}

function rowDetail(row) {
    if (!row)
        return "Activity recorded locally"
    if (row.completed)
        return row.itemLabel ? ("Completed · " + row.itemLabel) : "Completed"
    if (Number(row.watchSeconds || 0) > 0)
        return "Watched " + formatDuration(row.watchSeconds)
    if (Number(row.listenSeconds || 0) > 0)
        return "Listened " + formatDuration(row.listenSeconds)
    if (Number(row.pagesRead || 0) > 0)
        return "Read " + formatPages(row.pagesRead)
    if (Number(row.progressMicros || 0) > 0)
        return "Reading progress " + Math.round(Number(row.progressMicros) / 10000) + "%"
    return row.itemLabel || "Activity recorded locally"
}

function receiptsForTitle(deliveries, title) {
    var byProvider = ({})
    var source = sequenceValues(deliveries)
    var priority = { confirmed: 1, waiting: 2, syncing: 2, retrying: 3,
                     checking_delivery: 3, needs_attention: 4, failed: 4 }
    for (var i = 0; i < source.length; ++i) {
        var delivery = source[i] || ({})
        if (!title || String(delivery.title || "") !== title)
            continue
        var providerKey = String(delivery.providerKey || "")
        if (!providerKey)
            continue
        var projected = { providerKey: providerKey,
                          providerName: String(delivery.providerName || providerKey),
                          state: String(delivery.state || "waiting") }
        var previous = byProvider[providerKey]
        if (!previous || Number(priority[projected.state] || 0)
                >= Number(priority[previous.state] || 0))
            byProvider[providerKey] = projected
    }
    var result = []
    for (var key in byProvider)
        if (Object.prototype.hasOwnProperty.call(byProvider, key))
            result.push(byProvider[key])
    result.sort(function(left, right) {
        return left.providerName.localeCompare(right.providerName)
    })
    return result
}

function receiptLabel(receipts) {
    if (!receipts.length)
        return "Local only"
    for (var i = 0; i < receipts.length; ++i)
        if (receipts[i].state === "needs_attention" || receipts[i].state === "failed")
            return "Needs attention"
    for (var j = 0; j < receipts.length; ++j)
        if (receipts[j].state !== "confirmed")
            return "Sync pending"
    return "On " + receipts.length + (receipts.length === 1 ? " tracker" : " trackers")
}

function historyRows(projections, worldFilter, deliveries) {
    var result = []
    var source = sequenceValues(projections)
    var titleIdentities = ({})
    for (var identityProjection = 0; identityProjection < source.length; ++identityProjection) {
        var identityRows = sequenceValues(source[identityProjection].recentActivity)
        for (var identityIndex = 0; identityIndex < identityRows.length; ++identityIndex) {
            var identityRow = identityRows[identityIndex] || ({})
            var displayTitle = String(identityRow.title || "")
            var identityKey = String(identityRow.titleKey || "")
            if (!displayTitle || !identityKey)
                continue
            if (!titleIdentities[displayTitle])
                titleIdentities[displayTitle] = ({})
            titleIdentities[displayTitle][identityKey] = true
        }
    }
    for (var p = 0; p < source.length; ++p) {
        var rows = sequenceValues(source[p].recentActivity)
        for (var i = 0; i < rows.length; ++i) {
            var row = rows[i] || ({})
            if (worldFilter !== "all" && row.world !== worldFilter)
                continue
            var rowTitle = String(row.title || "")
            // The delivery boundary deliberately withholds canonical IDs. Only
            // decorate by display title when that title resolves to one local
            // activity identity; collisions stay local rather than guessing.
            var identities = titleIdentities[rowTitle] || ({})
            var receipts = Object.keys(identities).length <= 1
                    ? receiptsForTitle(deliveries, rowTitle) : []
            result.push({
                localDate: String(row.localDate || ""),
                lastAtMs: Number(row.lastAtMs || 0),
                sessionId: String(row.sessionId || ""),
                world: String(row.world || ""),
                worldLabel: worldName(row.world),
                kind: String(row.kind || ""),
                titleKey: String(row.titleKey || ""),
                itemKey: String(row.itemKey || ""),
                title: String(row.title || "Untitled activity"),
                detail: rowDetail(row),
                cover: String(row.cover || ""),
                completed: row.completed === true,
                syncLabel: receiptLabel(receipts),
                trackerReceipts: receipts
            })
        }
    }
    result.sort(function(left, right) {
        if (right.lastAtMs !== left.lastAtMs)
            return right.lastAtMs - left.lastAtMs
        return (left.kind + "|" + left.itemKey + "|" + left.sessionId)
            .localeCompare(right.kind + "|" + right.itemKey + "|" + right.sessionId)
    })
    return result
}

function titleMetric(row, category) {
    if (category === "watch") return Number(row.watchSeconds || 0)
    if (category === "listen") return Number(row.listenSeconds || 0)
    if (category === "pages") return Number(row.pagesRead || 0)
    if (category === "completed") return Number(row.completedCount || 0)
    if (category === "sessions") return Number(row.sessions || 0)
    if (category === "days") return Number(row.activeDays || 0)
    if (category === "time") return Number(row.watchSeconds || 0) + Number(row.listenSeconds || 0)
    return Number(row.watchSeconds || 0) + Number(row.listenSeconds || 0)
        + Number(row.pagesRead || 0) * 60 + Number(row.completedCount || 0) * 600
}

function metricText(row, category) {
    if (category === "watch") return formatDuration(row.watchSeconds)
    if (category === "listen") return formatDuration(row.listenSeconds)
    if (category === "pages") return formatPages(row.pagesRead)
    if (category === "completed") return formatCount(row.completedCount)
    if (category === "sessions") return formatCount(row.sessions)
    if (category === "days") return formatCount(row.activeDays)
    if (category === "time") return formatDuration(Number(row.watchSeconds || 0) + Number(row.listenSeconds || 0))
    if (Number(row.watchSeconds || 0) > 0) return formatDuration(row.watchSeconds)
    if (Number(row.listenSeconds || 0) > 0) return formatDuration(row.listenSeconds)
    if (Number(row.pagesRead || 0) > 0) return formatPages(row.pagesRead)
    if (Number(row.completedCount || 0) > 0) return formatCount(row.completedCount) + " completed"
    return formatCount(row.sessions) + " sessions"
}

function aggregateTitle(row) {
    var title = String(row && row.title || "Untitled activity")
    if (row && row.kind === "episode") {
        var showTitle = title.replace(/\s*[-–—]\s*S\d+E\d+(?:\s*[-–—:]\s*.*)?$/i, "").trim()
        if (showTitle)
            return showTitle
    }
    return title
}

function aggregateStats(projections, worldFilter, category) {
    var source = sequenceValues(projections)
    var filter = worldFilter || "all"
    var selectedCategory = category || "overview"
    var totals = { watchSeconds: 0, listenSeconds: 0, pagesRead: 0,
                   completedCount: 0, activeDays: 0 }
    var titleMap = ({})
    var filteredDates = ({})
    var completedItems = ({})

    for (var p = 0; p < source.length; ++p) {
        var projection = source[p] || ({})
        if (filter === "all") {
            totals.watchSeconds += Number(projection.watchSeconds || 0)
            totals.listenSeconds += Number(projection.listenSeconds || 0)
            totals.pagesRead += Number(projection.pagesRead || 0)
            totals.completedCount += Number(projection.completedCount || 0)
            totals.activeDays += Number(projection.activeDays || 0)
        }
        var moments = sequenceValues(projection.recentActivity)
        for (var i = 0; i < moments.length; ++i) {
            var moment = moments[i] || ({})
            if (filter !== "all" && moment.world !== filter)
                continue
            var completionKey = String(moment.kind || "") + "|" + String(moment.itemKey || "")
            var firstCompletion = moment.completed === true && !completedItems[completionKey]
            if (firstCompletion)
                completedItems[completionKey] = true
            if (filter !== "all") {
                totals.watchSeconds += Number(moment.watchSeconds || 0)
                totals.listenSeconds += Number(moment.listenSeconds || 0)
                totals.pagesRead += Number(moment.pagesRead || 0)
                totals.completedCount += firstCompletion ? 1 : 0
                if (moment.localDate) filteredDates[String(moment.localDate)] = true
            }
            var key = String(moment.titleKey || (moment.world + "|" + moment.title))
            if (!titleMap[key]) {
                titleMap[key] = { titleKey: key, title: aggregateTitle(moment),
                    cover: String(moment.cover || ""), world: String(moment.world || ""),
                    watchSeconds: 0, listenSeconds: 0, pagesRead: 0, completedCount: 0,
                    sessions: 0, dates: ({}), latestAtMs: 0 }
            }
            var title = titleMap[key]
            title.watchSeconds += Number(moment.watchSeconds || 0)
            title.listenSeconds += Number(moment.listenSeconds || 0)
            title.pagesRead += Number(moment.pagesRead || 0)
            title.completedCount += firstCompletion ? 1 : 0
            title.sessions += 1
            if (moment.localDate) title.dates[String(moment.localDate)] = true
            if (Number(moment.lastAtMs || 0) >= title.latestAtMs) {
                title.latestAtMs = Number(moment.lastAtMs || 0)
                title.title = aggregateTitle(moment)
                title.cover = String(moment.cover || title.cover)
                title.world = String(moment.world || title.world)
            }
        }
    }
    if (filter !== "all")
        totals.activeDays = Object.keys(filteredDates).length

    var rows = []
    for (var key in titleMap) {
        var sourceRow = titleMap[key]
        sourceRow.activeDays = Object.keys(sourceRow.dates).length
        var metric = titleMetric(sourceRow, selectedCategory)
        if (selectedCategory !== "overview" && metric <= 0)
            continue
        rows.push({ titleKey: sourceRow.titleKey, title: sourceRow.title, cover: sourceRow.cover,
            world: sourceRow.world, worldLabel: worldName(sourceRow.world),
            value: metricText(sourceRow, selectedCategory), metric: metric,
            details: formatCount(sourceRow.sessions) + (sourceRow.sessions === 1 ? " session" : " sessions"),
            latestAtMs: sourceRow.latestAtMs })
    }
    rows.sort(function(left, right) {
        if (selectedCategory === "overview" && right.metric === left.metric
                && right.latestAtMs !== left.latestAtMs)
            return right.latestAtMs - left.latestAtMs
        if (right.metric !== left.metric)
            return right.metric - left.metric
        return left.title.localeCompare(right.title)
    })

    return {
        summary: [
            { value: formatDuration(totals.watchSeconds), label: "Watch time" },
            { value: formatCount(totals.pagesRead), label: "Pages read" },
            { value: formatDuration(totals.listenSeconds), label: "Listened" },
            { value: formatCount(totals.completedCount), label: "Completed" },
            { value: formatCount(totals.activeDays), label: "Active days" }
        ],
        rows: rows,
        empty: rows.length === 0
    }
}

function highlightCards(projection) {
    var rows = projection ? sequenceValues(projection.highlights) : []
    var result = []
    for (var i = 0; i < rows.length; ++i) {
        var row = rows[i] || ({})
        var role = String(row.role || "recent")
        var label = "Recently active"
        var value = "—"
        if (role === "theatre") { label = "Most watched"; value = formatDuration(row.watchSeconds) }
        else if (role === "tankoban") { label = "Most read"; value = formatPages(row.pagesRead) }
        else if (role === "biblio_ebook") {
            label = "Most read book"
            value = Number(row.pagesRead || 0) > 0 ? formatPages(row.pagesRead)
                  : Math.round(Number(row.progressMicros || 0) / 10000) + "%"
        } else if (role === "audiobook") { label = "Most listened"; value = formatDuration(row.listenSeconds) }
        else if (role === "completion") { label = "Completed"; value = formatCount(row.completedCount) }
        else if (Number(row.watchSeconds || 0) > 0) value = formatDuration(row.watchSeconds)
        else if (Number(row.listenSeconds || 0) > 0) value = formatDuration(row.listenSeconds)
        else if (Number(row.pagesRead || 0) > 0) value = formatPages(row.pagesRead)
        result.push({ title: String(row.title || "Untitled activity"), cover: String(row.cover || ""),
            world: String(row.world || ""), label: label, value: value })
    }
    return result
}
