.pragma library

function copyHeaders(headers) {
    var out = ({})
    if (!headers || typeof headers !== "object" || Array.isArray(headers))
        return out
    var keys = Object.keys(headers)
    for (var i = 0; i < keys.length; i++)
        out[keys[i]] = headers[keys[i]]
    return out
}

function _parseIpv4(host) {
    var parts = String(host || "").split(".")
    if (parts.length !== 4)
        return null
    var out = []
    for (var i = 0; i < parts.length; i++) {
        if (!/^\d{1,3}$/.test(parts[i]))
            return null
        if (parts[i].length > 1 && parts[i][0] === "0")
            return null
        var value = parseInt(parts[i], 10)
        if (value < 0 || value > 255)
            return null
        out.push(value)
    }
    return out
}
function _isPublicIpv4(ip) {
    if (!ip)
        return false
    var a = ip[0], b = ip[1]
    if (a === 0 || a === 10 || a === 127 || a >= 224)
        return false
    if (a === 100 && b >= 64 && b <= 127)
        return false
    if (a === 169 && b === 254)
        return false
    if (a === 172 && b >= 16 && b <= 31)
        return false
    if (a === 192 && b === 168)
        return false
    if (a === 198 && (b === 18 || b === 19))
        return false
    return true
}

function _isPublicIpv6(host) {
    var lower = String(host || "").toLowerCase()
    if (lower === "::" || lower === "::1")
        return false
    if (lower.indexOf("fc") === 0 || lower.indexOf("fd") === 0 || lower.indexOf("ff") === 0)
        return false
    if (/^fe[89ab]/.test(lower))
        return false
    var dotted = lower.substring(lower.lastIndexOf(":") + 1)
    if (/^\d+\.\d+\.\d+\.\d+$/.test(dotted))
        return _isPublicIpv4(_parseIpv4(dotted))
    return true
}
function _validPort(text) {
    if (!/^\d+$/.test(text))
        return false
    var port = parseInt(text, 10)
    return port >= 1 && port <= 65535
}

function admitProviderUrl(value) {
    var raw = String(value || "")
    var match = /^https:\/\/([^\/?#]+)(?:[\/?#]|$)/i.exec(raw)
    if (!match)
        return ""
    var authority = match[1]
    if (!authority.length || /[\\\s%]/.test(authority))
        return ""
    var at = authority.lastIndexOf("@")
    var hostPort = at >= 0 ? authority.substring(at + 1) : authority
    if (!hostPort.length)
        return ""

    var host = ""
    if (hostPort[0] === "[") {
        var close = hostPort.indexOf("]")
        if (close <= 1)
            return ""
        host = hostPort.substring(1, close)
        var tail = hostPort.substring(close + 1)
        if (tail.length && (tail[0] !== ":" || !_validPort(tail.substring(1))))
            return ""
        if (host.indexOf(":") < 0 || !_isPublicIpv6(host))
            return ""
    } else {
        var colon = hostPort.lastIndexOf(":")
        if (colon >= 0) {
            if (hostPort.indexOf(":") !== colon || !_validPort(hostPort.substring(colon + 1)))
                return ""
            host = hostPort.substring(0, colon)
        } else {
            host = hostPort
        }
    }

    var lower = host.toLowerCase()
    while (lower.length > 1 && lower[lower.length - 1] === ".")
        lower = lower.substring(0, lower.length - 1)
    if (!lower.length || lower === "localhost" || /\.localhost$/.test(lower) || /\.local$/.test(lower))
        return ""
    if (/^\d+$/.test(lower) || /^0x[0-9a-f]+$/.test(lower))
        return ""
    if (/^\d+\.\d+\.\d+\.\d+$/.test(lower)) {
        var ipv4 = _parseIpv4(lower)
        if (!_isPublicIpv4(ipv4))
            return ""
    }
    return raw
}
