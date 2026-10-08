.pragma library

// Engine callbacks outlive document epochs. Keep their entry point outside a
// disposable QML context and sever all view/application references on terminal.
function ticket(owner, id, epoch, callback) {
    return {owner:owner, id:id, epoch:epoch, callback:callback, valid:true}
}
function invalidate(ticket) {
    ticket.valid = false
    ticket.owner = null
    ticket.callback = null
}
function resultCallback(ticket) {
    return function(result) {
        // This must precede every access to a view or its QML callback.
        if (!ticket.valid) return
        var owner = ticket.owner, callback = ticket.callback
        invalidate(ticket)
        owner.finishJavaScript(ticket, result, callback)
    }
}
