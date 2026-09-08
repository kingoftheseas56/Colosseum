// KeyboardRegion — one logical Tab stop and focus boundary.
//
// Regions own focus entry/return policy. Indexed movement remains in
// KeyboardCollectionController; this type only decides when focus enters,
// leaves, wraps, or returns from a composite surface.
import QtQuick

FocusScope {
    id: region

    property string regionId: ""
    property Item entryItem: null
    property Item lastFocusItem: null
    property Item tabNext: null
    property Item tabPrevious: null
    property Item returnFocusItem: null
    property bool trapTab: false
    property var returnSnapshot: null
    property alias spatialNavigator: spatialNav

    signal escapeRequested()
    signal boundaryTabRequested(bool forward)

    KeyboardSpatialNavigator {
        id: spatialNav
        root: region
    }

    onActiveFocusChanged: {
        if (!region.activeFocus)
            spatialNav.cancelNavigation("focus")
    }

    function _isVisibleEnabled(item) {
        return item && item.visible === true && item.enabled === true
    }

    function _isInternal(item) {
        for (var node = item; node; node = node.parent) {
            if (node === region)
                return true
        }
        return false
    }

    function _isFocusable(item, internalOnly) {
        if (!item || (internalOnly && !_isInternal(item)) || !_isVisibleEnabled(item))
            return false
        if (item === region)
            return false
        if (item.focusPolicy !== undefined && item.focusPolicy === Qt.NoFocus)
            return false
        return item.activeFocusOnTab === true
            || (item.focusPolicy !== undefined && item.focusPolicy !== Qt.NoFocus)
    }

    function _appendFocusable(item, result) {
        if (!item || !item.visible || !item.enabled)
            return
        if (_isFocusable(item, true))
            result.push(item)
        var children = item.children || []
        for (var i = 0; i < children.length; i++)
            _appendFocusable(children[i], result)
    }

    function focusableItems() {
        var result = []
        var children = region.children || []
        for (var i = 0; i < children.length; i++)
            _appendFocusable(children[i], result)
        return result
    }

    function _activeFocusable(item) {
        var children = item.children || []
        for (var i = 0; i < children.length; i++) {
            var child = children[i]
            if (!child || !child.visible || !child.enabled)
                continue
            var nested = _activeFocusable(child)
            if (nested)
                return nested
            if (_isFocusable(child, true) && child.activeFocus)
                return child
        }
        return null
    }

    function _validInternal(item) {
        return _isFocusable(item, true)
    }

    function _stableIdentity(item) {
        if (!item)
            return ""
        if (item.stableId !== undefined && item.stableId !== null)
            return String(item.stableId)
        return item.objectName || ""
    }

    function _scrollSnapshot(item) {
        var result = []
        for (var node = item; node; node = node.parent) {
            if (node.contentX !== undefined && node.contentY !== undefined)
                result.push({ flick: node, x: Number(node.contentX), y: Number(node.contentY) })
            if (node === region)
                break
        }
        return result
    }

    function _snapshotTarget(snapshot) {
        if (!snapshot)
            return null
        if (_validInternal(snapshot.item))
            return snapshot.item
        if (!snapshot.identity)
            return null
        var items = region.focusableItems()
        for (var i = 0; i < items.length; ++i) {
            if (_stableIdentity(items[i]) === snapshot.identity)
                return items[i]
        }
        return null
    }

    function _restoreScrollSnapshot(snapshot) {
        if (!snapshot || !snapshot.scrolls)
            return
        for (var i = 0; i < snapshot.scrolls.length; ++i) {
            var saved = snapshot.scrolls[i]
            if (!saved.flick || saved.flick.visible === false || saved.flick.enabled === false)
                continue
            if (saved.flick.contentX !== undefined)
                saved.flick.contentX = saved.x
            if (saved.flick.contentY !== undefined)
                saved.flick.contentY = saved.y
        }
    }

    function _focus(item, reason) {
        if (!_isFocusable(item, false))
            return false
        item.forceActiveFocus(reason === undefined ? Qt.OtherFocusReason : reason)
        if (item.activeFocus)
            region.lastFocusItem = item
        return item.activeFocus === true
    }

    function focusEntry() {
        var items = region.focusableItems()
        if (items.length === 0)
            return false

        var target = _validInternal(region.lastFocusItem) ? region.lastFocusItem : null
        if (!target && _validInternal(region.entryItem))
            target = region.entryItem
        if (!target)
            target = items[0]
        return _focus(target, Qt.TabFocusReason)
    }

    function rememberFocus(item) {
        if (!_validInternal(item))
            return false
        region.lastFocusItem = item
        region.returnSnapshot = {
            item: item,
            identity: _stableIdentity(item),
            scrolls: _scrollSnapshot(item)
        }
        return true
    }

    function restoreFocus() {
        if (_isFocusable(region.returnFocusItem, false))
            return _focus(region.returnFocusItem, Qt.PopupFocusReason)
        var snapshotTarget = _snapshotTarget(region.returnSnapshot)
        if (snapshotTarget) {
            _restoreScrollSnapshot(region.returnSnapshot)
            return _focus(snapshotTarget, Qt.PopupFocusReason)
        }
        // A removed invoker must not keep stale identity or scroll state alive
        // for a later, unrelated route return.
        region.returnSnapshot = null
        return region.focusEntry()
    }

    function _tabTarget(forward) {
        var items = region.focusableItems()
        if (items.length === 0)
            return null

        var current = _activeFocusable(region)
        var index = items.indexOf(current)
        if (index < 0 && _validInternal(region.lastFocusItem))
            index = items.indexOf(region.lastFocusItem)
        if (index < 0)
            index = forward ? -1 : items.length

        var nextIndex = forward ? index + 1 : index - 1
        if (nextIndex >= 0 && nextIndex < items.length)
            return items[nextIndex]

        var boundary = forward ? region.tabNext : region.tabPrevious
        if (_isFocusable(boundary, false))
            return boundary
        if (region.trapTab)
            return forward ? items[0] : items[items.length - 1]
        return null
    }

    function handleKey(key, modifiers, event) {
        var mods = modifiers === undefined ? Qt.NoModifier : modifiers
        if (key === Qt.Key_Escape) {
            spatialNav.cancelNavigation("escape")
            region.escapeRequested()
            if (event)
                event.accepted = true
            return true
        }

        if (spatialNav.isDirectionalKey(key)) {
            if (mods & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return false
            var moved = spatialNav.handle({ key: key, modifiers: mods, accepted: false })
            if (moved && event)
                event.accepted = true
            return moved
        }

        var backward = key === Qt.Key_Backtab
            || (key === Qt.Key_Tab && (mods & Qt.ShiftModifier) !== 0)
        if (key !== Qt.Key_Tab && key !== Qt.Key_Backtab)
            return false

        var target = region._tabTarget(!backward)
        if (!target) {
            region.boundaryTabRequested(!backward)
            return false
        }
        if (!_focus(target, backward ? Qt.BacktabFocusReason : Qt.TabFocusReason))
            return false
        if (event)
            event.accepted = true
        return true
    }

    Keys.onPressed: function(event) {
        region.handleKey(event.key, event.modifiers, event)
    }

    Keys.onReleased: function(event) {
        spatialNav.handleRelease(event)
    }
}
