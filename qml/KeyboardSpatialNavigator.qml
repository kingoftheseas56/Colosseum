// KeyboardSpatialNavigator — PlayStation-style spatial focus movement.
//
// The focused control remains the authority for keys it understands. Owners call
// handle() only for an arrow that bubbles out unaccepted; this navigator then picks
// the best visible focusable target in the requested visual direction. Direction
// alignment outranks raw distance, followed by lane overlap and primary/cross-axis
// distance. No candidate means "boundary": the key stays unaccepted for an outer
// owner to handle.
import QtQuick
import "KeyboardViewport.js" as Viewport

Item {
    id: nav

    required property Item root
    property real minimumPrimaryDistance: 6
    property bool preserveEditableArrows: true
    property real scrollStep: 72
    // A target larger than a clip remains identifiable only when at least one
    // quarter of the viewport is visible on each oversized axis.
    property real identifiablePortionRatio: 0.25
    // Platform key repeat is the only repeat source.  A realization callback may
    // retain one pending landing, but it must belong to the current input
    // generation before it can focus anything.
    property int navigationGeneration: 0
    property int activeNavigationKey: -1
    property int heldNavigationKey: -1
    property bool navigationActive: false
    property var pendingNavigation: null
    property var pendingSectionReturn: null

    signal boundaryRequested(int key, Item fromItem)
    signal navigationCancelled(string reason)

    visible: false
    width: 0
    height: 0

    Connections {
        target: nav.root
        function onActiveFocusChanged() {
            if (!nav.root || !nav.root.activeFocus)
                nav.cancelNavigation("focus")
        }
        function onVisibleChanged() {
            if (!nav.root || !nav.root.visible)
                nav.cancelNavigation("route")
        }
        function onEnabledChanged() {
            if (!nav.root || !nav.root.enabled)
                nav.cancelNavigation("route")
        }
    }

    function isDirectionalKey(key) {
        return key === Qt.Key_Up || key === Qt.Key_Down
            || key === Qt.Key_Left || key === Qt.Key_Right
    }

    function _isDescendant(item) {
        for (var node = item; node; node = node.parent) {
            if (node === nav.root)
                return true
        }
        return false
    }

    function _isEditable(item) {
        if (!item || !nav.preserveEditableArrows)
            return false
        return item.text !== undefined
            && item.cursorPosition !== undefined
            && (item.readOnly === undefined || item.readOnly === false)
    }

    function _eligible(item) {
        if (!item || !nav.root || !nav._isDescendant(item))
            return false

        var node = item
        while (node) {
            if (node.visible === false || node.enabled === false)
                return false
            if (node.opacity !== undefined && Number(node.opacity) <= 0.01)
                return false
            if (node === nav.root)
                break
            node = node.parent
        }
        if (node !== nav.root)
            return false

        if (Number(item.width) <= 0 || Number(item.height) <= 0)
            return false

        return true
    }

    function _rectIn(item, ancestor) {
        var p0 = item.mapToItem(ancestor, 0, 0)
        var p1 = item.mapToItem(ancestor, Number(item.width), Number(item.height))
        return {
            left: Math.min(p0.x, p1.x), right: Math.max(p0.x, p1.x),
            top: Math.min(p0.y, p1.y), bottom: Math.max(p0.y, p1.y)
        }
    }

    function _rectIntersectsPoint(item, point, root) {
        if (!item || item.visible === false || item.enabled === false
                || (item.opacity !== undefined && Number(item.opacity) <= 0.01))
            return false
        var rect = nav._rectIn(item, root)
        return point.x >= rect.left && point.x <= rect.right
            && point.y >= rect.top && point.y <= rect.bottom
    }

    function _coveredByHigherSibling(item) {
        if (!item || !nav.root)
            return false
        var center = item.mapToItem(nav.root, Number(item.width) / 2,
                                    Number(item.height) / 2)
        var branch = item
        while (branch && branch.parent) {
            var parent = branch.parent
            var siblings = parent.children || []
            var branchZ = Number(branch.z || 0)
            for (var i = 0; i < siblings.length; ++i) {
                var sibling = siblings[i]
                if (sibling !== branch && Number(sibling.z || 0) > branchZ
                        && nav._rectIntersectsPoint(sibling, center, nav.root))
                    return true
            }
            if (parent === nav.root)
                break
            branch = parent
        }
        return false
    }

    function _centerVisibleThroughClips(item) {
        if (!nav._eligible(item))
            return false

        for (var ancestor = item.parent; ancestor; ancestor = ancestor.parent) {
            if (ancestor.clip === true || ancestor === nav.root) {
                var rect = nav._rectIn(item, ancestor)
                var viewportWidth = Number(ancestor.width)
                var viewportHeight = Number(ancestor.height)
                var targetWidth = rect.right - rect.left
                var targetHeight = rect.bottom - rect.top
                var oversizedWidth = targetWidth > viewportWidth + 0.000001
                var oversizedHeight = targetHeight > viewportHeight + 0.000001
                if (oversizedWidth) {
                    var overlapWidth = Math.min(rect.right, viewportWidth)
                        - Math.max(rect.left, 0)
                    if (overlapWidth < viewportWidth * nav.identifiablePortionRatio)
                        return false
                } else if (rect.left < -0.000001
                           || rect.right > viewportWidth + 0.000001) {
                    return false
                }
                if (oversizedHeight) {
                    var overlapHeight = Math.min(rect.bottom, viewportHeight)
                        - Math.max(rect.top, 0)
                    if (overlapHeight < viewportHeight * nav.identifiablePortionRatio)
                        return false
                } else if (rect.top < -0.000001
                           || rect.bottom > viewportHeight + 0.000001) {
                    return false
                }
            }
            if (ancestor === nav.root)
                break
        }
        return true
    }

    function _landingEligible(item) {
        return nav._centerVisibleThroughClips(item)
            && !nav._coveredByHigherSibling(item)
    }

    function _isFocusable(item, includeOffscreen) {
        if (!item || item === nav || item === nav.root
                || !(includeOffscreen ? nav._eligible(item) : nav._landingEligible(item)))
            return false
        if (item.focusPolicy !== undefined)
            return item.focusPolicy !== Qt.NoFocus
        return item.activeFocusOnTab === true
    }

    function _appendFocusable(node, result, includeOffscreen) {
        if (!node || node === nav || node.visible === false || node.enabled === false)
            return 0

        var before = result.length
        var children = node.children || []
        for (var i = 0; i < children.length; i++)
            nav._appendFocusable(children[i], result, includeOffscreen)

        // Hand-built controls sometimes leave a legacy focusable wrapper around a
        // KeyboardAction. Prefer the deepest focusable face so one visual control is
        // one D-pad stop rather than two stacked stops at identical geometry.
        if (node !== nav.root && result.length === before && nav._isFocusable(node, includeOffscreen))
            result.push(node)
        return result.length - before
    }

    function focusableItems() {
        var result = []
        if (nav.root)
            nav._appendFocusable(nav.root, result)
        return result
    }

    function focusNamed(name, reason) {
        if (!name)
            return false
        var items = nav.focusableItems()
        for (var i = 0; i < items.length; i++) {
            if (items[i].objectName !== name)
                continue
            items[i].forceActiveFocus(reason === undefined ? Qt.TabFocusReason : reason)
            return items[i].activeFocus === true
        }
        return false
    }

    function _activeItem(node) {
        if (!node || node === nav)
            return null
        var children = node.children || []
        for (var i = 0; i < children.length; i++) {
            var childActive = nav._activeItem(children[i])
            if (childActive)
                return childActive
        }
        return node.activeFocus === true ? node : null
    }

    function activeItem() {
        var focused = nav.root ? nav._activeItem(nav.root) : null
        // Native/editable controls keep first claim even when nested inside a
        // collection-managed delegate. Semantic projection is only for the
        // owner after the actual focused control has declined the key.
        if (nav._isEditable(focused))
            return focused
        var owner = nav._collectionOwner(focused)
        if (owner && owner.currentIndex !== undefined) {
            var selected = owner.keyboardItemAtIndex
                ? owner.keyboardItemAtIndex(Number(owner.currentIndex))
                : (owner.itemAtIndex ? owner.itemAtIndex(Number(owner.currentIndex)) : null)
            if (selected)
                return selected
        }
        return focused
    }

    function beginNavigation(key) {
        if (!nav.isDirectionalKey(key))
            return nav.navigationGeneration
        if (nav.navigationActive && nav.activeNavigationKey !== key)
            nav.cancelNavigation("direction")
        // Every logical press, including platform autorepeat for the same key,
        // receives a fresh intent generation. Held-key identity is tracked
        // separately so only its terminal release cancels the active intent.
        nav.navigationGeneration += 1
        nav.navigationActive = true
        nav.activeNavigationKey = key
        nav.heldNavigationKey = key
        return nav.navigationGeneration
    }

    function isNavigationGenerationCurrent(generation, key) {
        if (!nav.navigationActive || Number(generation) !== nav.navigationGeneration)
            return false
        return key === undefined || key === null || nav.activeNavigationKey === key
    }

    function cancelNavigation(reason) {
        nav.navigationGeneration += 1
        nav.pendingNavigation = null
        nav.pendingSectionReturn = null
        nav.navigationActive = false
        nav.activeNavigationKey = -1
        nav.heldNavigationKey = -1
        nav.navigationCancelled(reason || "cancelled")
    }

    // Keep a single deferred landing slot.  This is intentionally not a queue:
    // every newer key replaces the prior intent, and release/focus/route loss
    // invalidates the generation before a realization callback can land.
    function deferLanding(target, key, reason, generation) {
        var token = generation === undefined ? nav.beginNavigation(key) : Number(generation)
        if (!nav.isNavigationGenerationCurrent(token, key) || !target)
            return false
        nav.pendingNavigation = { target: target, key: key,
                                  reason: reason === undefined ? Qt.OtherFocusReason : reason,
                                  generation: token }
        return true
    }

    function settlePendingLanding() {
        var pending = nav.pendingNavigation
        nav.pendingNavigation = null
        if (!pending || !nav.isNavigationGenerationCurrent(pending.generation, pending.key))
            return false
        if (pending.sectionOwner) {
            var owner = pending.sectionOwner
            var coordinator = pending.sectionCoordinator
            if (!owner || !nav.root || nav.root.visible === false || nav.root.enabled === false
                    || owner.visible === false || owner.enabled === false
                    || (owner.opacity !== undefined && Number(owner.opacity) <= 0.01)
                    || !Viewport.contains(nav.root, owner)
                    || nav._hasUnsupportedTransform(owner)) {
                if (coordinator && coordinator.clear)
                    coordinator.clear()
                nav.pendingSectionReturn = null
                return false
            }
            if (pending.revision !== undefined
                    && nav._collectionModelRevision(owner) !== pending.revision) {
                if (coordinator && coordinator.clear)
                    coordinator.clear()
                nav.pendingSectionReturn = null
                return false
            }
            var index = pending.index
            if (owner.keyboardIndexForIdentity && pending.identity) {
                var resolved = Number(owner.keyboardIndexForIdentity(pending.identity))
                if (resolved >= 0)
                    index = resolved
            }
            var target = owner.keyboardItemAtIndex ? owner.keyboardItemAtIndex(index)
                    : (owner.itemAtIndex ? owner.itemAtIndex(index) : owner.currentItem)
            if (!target || !nav._eligible(target)
                    || (pending.identity && nav._collectionIdentity(target) !== pending.identity)) {
                if (coordinator && coordinator.clear)
                    coordinator.clear()
                nav.pendingSectionReturn = null
                return false
            }
            nav.pendingSectionReturn = null
            var controller = nav._scrollControllerFor(owner)
            if (controller && controller.arrowScrolling === false)
                return false
            var ok = nav._land(target, pending.key, pending.reason,
                               nav._directionalStep(owner,
                                   pending.key === Qt.Key_Left || pending.key === Qt.Key_Right,
                                   controller))
            if (ok && coordinator && coordinator.clear)
                coordinator.clear()
            return ok
        }
        var owner = nav._flickableOwner(pending.target)
        var controller = owner ? nav._scrollControllerFor(owner) : null
        var horizontal = pending.key === Qt.Key_Left || pending.key === Qt.Key_Right
        var budget = owner ? nav._directionalStep(owner, horizontal, controller)
                           : nav.scrollStep
        if (controller && controller.arrowScrolling === false)
            return false
        return nav._land(pending.target, pending.key, pending.reason, budget)
    }

    function _collectionModelRevision(owner) {
        if (!owner)
            return undefined
        if (owner.keyboardModelRevision !== undefined)
            return Number(owner.keyboardModelRevision)
        if (owner.modelRevision !== undefined)
            return Number(owner.modelRevision)
        return undefined
    }

    function _scheduleSectionReturn(owner, index, identity, coordinator, key, reason) {
        var generation = nav.beginNavigation(key)
        nav.pendingSectionReturn = { owner: owner, index: index, identity: identity,
                                     coordinator: coordinator,
                                     revision: nav._collectionModelRevision(owner),
                                     key: key, reason: reason, generation: generation }
        nav.pendingNavigation = { sectionOwner: owner, index: index, identity: identity,
                                  sectionCoordinator: coordinator,
                                  revision: nav._collectionModelRevision(owner),
                                  key: key, reason: reason, generation: generation }
        Qt.callLater(nav.settlePendingLanding)
        return true
    }

    function handleRelease(event) {
        if (!event || !nav.isDirectionalKey(event.key))
            return false
        // Platform auto-repeat releases are not the terminal release. Native
        // owners may surface them while a held key is still active.
        if (event.isAutoRepeat === true)
            return true
        if (nav.navigationActive && nav.heldNavigationKey === event.key)
            nav.cancelNavigation("release")
        return true
    }

    function _scrollControllerFor(flick) {
        if (!nav.root || !flick)
            return null
        var registered = Viewport.controllerFor(flick)
        if (registered)
            return registered
        var pending = [nav.root]
        while (pending.length > 0) {
            var node = pending.shift()
            if (node !== nav && node.flick !== undefined && node.flick === flick
                    && node.lineStep !== undefined && node.arrowScrolling !== undefined)
                return node
            var children = node.children || []
            for (var i = 0; i < children.length; ++i)
                pending.push(children[i])
        }
        return null
    }

    function _flickableOwner(item) {
        for (var node = item; node; node = node.parent) {
            if (Viewport.isFlickable(node))
                return node
            if (node === nav.root)
                break
        }
        return null
    }

    function _collectionOwner(item) {
        for (var node = item; node; node = node.parent) {
            if (node.keyboardReturnOwner === true
                    && node.keyboardIdentityForIndex !== undefined
                    && node.keyboardIndexForIdentity !== undefined)
                return node
            if (node === nav.root)
                break
        }
        return null
    }

    function _sectionCoordinator(owner) {
        return owner && owner.keyboardSectionCoordinator
                ? owner.keyboardSectionCoordinator : null
    }

    function _collectionIndex(owner, item) {
        var delegate = nav._collectionDelegate(owner, item)
        if (!delegate)
            return -1
        if (delegate.index !== undefined && isFinite(Number(delegate.index)))
            return Number(delegate.index)
        var identity = nav._collectionIdentity(delegate)
        return owner.keyboardIndexForIdentity && identity
            ? Number(owner.keyboardIndexForIdentity(identity)) : -1
    }

    function _collectionIdentity(item) {
        if (!item)
            return ""
        if (item.stableId !== undefined && item.stableId !== null && item.stableId !== "")
            return String(item.stableId)
        if (item.entry && item.entry.id !== undefined)
            return String(item.entry.id)
        if (item.slide) {
            if (item.slide.raw && item.slide.raw.id !== undefined)
                return String(item.slide.raw.id)
            if (item.slide.id !== undefined)
                return String(item.slide.id)
            if (item.slide.title !== undefined)
                return String(item.slide.title)
        }
        if (item.objectName !== undefined && item.objectName !== "")
            return String(item.objectName)
        return ""
    }

    // A spatial candidate is often a nested KeyboardAction, while collection
    // identity belongs to its delegate root. Walk the existing owner hierarchy
    // and use the delegate's index or identity seam; never scan the model.
    function _collectionDelegate(owner, item) {
        if (!owner || !item)
            return null
        for (var node = item; node && node !== owner; node = node.parent) {
            if (node.index !== undefined && isFinite(Number(node.index)))
                return node
            var identity = nav._collectionIdentity(node)
            if (identity && owner.keyboardIndexForIdentity
                    && Number(owner.keyboardIndexForIdentity(identity)) >= 0)
                return node
        }
        return null
    }

    function _collectionFocusOwner(item) {
        var owner = nav._collectionOwner(item)
        if (!owner || owner.keyboardReturnOwner !== true)
            return null
        return nav._collectionDelegate(owner, item) ? owner : null
    }

    function _selectCollectionItem(owner, item) {
        var index = nav._collectionIndex(owner, item)
        if (index < 0)
            return false
        if (owner.currentIndex !== undefined)
            owner.currentIndex = index
        return owner.currentIndex === undefined || Number(owner.currentIndex) === index
    }

    function _rememberSectionTransition(fromItem, target) {
        var sourceOwner = nav._collectionOwner(fromItem)
        var targetOwner = nav._collectionOwner(target)
        if (!sourceOwner || !targetOwner || sourceOwner === targetOwner)
            return
        var coordinator = nav._sectionCoordinator(sourceOwner)
        if (!coordinator)
            coordinator = nav._sectionCoordinator(targetOwner)
        if (!coordinator || !coordinator.remember)
            return
        var sourceIndex = sourceOwner.currentIndex !== undefined
                ? Number(sourceOwner.currentIndex) : nav._collectionIndex(sourceOwner, fromItem)
        var identity = sourceOwner.keyboardIdentityForIndex(sourceIndex)
        var offset = sourceOwner.contentX !== undefined ? Number(sourceOwner.contentX) : 0
        coordinator.remember(sourceOwner, sourceIndex, identity, offset)
        nav._selectCollectionItem(targetOwner, target)
    }

    function _restoreSectionReturn(fromItem, key, reason) {
        if (key !== Qt.Key_Up)
            return false
        var currentOwner = nav._collectionOwner(fromItem)
        var coordinator = nav._sectionCoordinator(currentOwner)
        if (!currentOwner || !coordinator || !coordinator.returnRecord)
            return false
        var record = coordinator.returnRecord(currentOwner)
        if (!record || !record.owner)
            return false
        var owner = record.owner
        if (!nav.root || nav.root.visible === false || nav.root.enabled === false
                || owner.visible === false || owner.enabled === false
                || (owner.opacity !== undefined && Number(owner.opacity) <= 0.01)
                || !Viewport.contains(nav.root, owner)
                || nav._hasUnsupportedTransform(owner)) {
            coordinator.clear()
            return false
        }
        var ownerController = nav._scrollControllerFor(owner)
        if (ownerController && ownerController.arrowScrolling === false) {
            coordinator.clear()
            return false
        }
        var count = owner.keyboardItems && owner.keyboardItems.length !== undefined
                ? Number(owner.keyboardItems.length) : Number(owner.count || 0)
        var index = owner.keyboardIndexForIdentity
                ? Number(owner.keyboardIndexForIdentity(record.identity)) : -1
        if (index < 0 && count > 0)
            index = Math.max(0, Math.min(count - 1, Number(record.index)))
        if (index < 0 || count <= 0)
            return false
        if (owner.currentIndex !== undefined)
            owner.currentIndex = index
        if (owner.contentX !== undefined && isFinite(Number(record.offset))
                && Viewport.isFlickable(owner))
            Viewport.setPosition(owner, true, Number(record.offset))
        var target = owner.keyboardItemAtIndex ? owner.keyboardItemAtIndex(index)
                : (owner.itemAtIndex ? owner.itemAtIndex(index) : owner.currentItem)
        if (!target) {
            if (owner.keyboardRevealIndex)
                owner.keyboardRevealIndex(index)
            else if (owner.positionViewAtIndex)
                owner.positionViewAtIndex(index, owner.positionMode)
            target = owner.keyboardItemAtIndex ? owner.keyboardItemAtIndex(index)
                    : (owner.itemAtIndex ? owner.itemAtIndex(index) : owner.currentItem)
        }
        if (!target) {
            owner.forceActiveFocus(reason)
            if (nav._scheduleSectionReturn(owner, index, String(record.identity || ""),
                                           coordinator, key, reason))
                return true
        }
        if (!target || !nav._eligible(target) || nav._hasUnsupportedTransform(target)) {
            coordinator.clear()
            return false
        }
        if (owner.keyboardRevealIndex)
            owner.keyboardRevealIndex(index)
        else if (owner.positionViewAtIndex)
            owner.positionViewAtIndex(index, owner.positionMode)
        target = owner.keyboardItemAtIndex ? owner.keyboardItemAtIndex(index)
                : (owner.itemAtIndex ? owner.itemAtIndex(index) : target)
        if (target && !_centerVisibleThroughClips(target)) {
            var targetRect = nav._rectIn(target, owner)
            if (targetRect.left < 0 && owner.contentX !== undefined)
                owner.contentX += targetRect.left
            else if (targetRect.right > Number(owner.width) && owner.contentX !== undefined)
                owner.contentX += targetRect.right - Number(owner.width)
        }
        target = owner.keyboardItemAtIndex ? owner.keyboardItemAtIndex(index)
                : (owner.itemAtIndex ? owner.itemAtIndex(index) : target)
        if (!target || !nav._centerVisibleThroughClips(target))
            return false
        var targetOwner = nav._collectionFocusOwner(target)
        if (targetOwner && nav._selectCollectionItem(targetOwner, target)) {
            targetOwner.forceActiveFocus(reason)
            if (targetOwner.activeFocus === true)
                coordinator.clear()
            return targetOwner.activeFocus === true
        }
        target.forceActiveFocus(reason)
        if (target.activeFocus === true)
            coordinator.clear()
        return target.activeFocus === true
    }

    function _rootDistanceForLocalDelta(flick, horizontal, localDelta) {
        if (!flick || !nav.root)
            return Math.abs(Number(localDelta))
        var before = flick.mapToItem(nav.root, 0, 0)
        var after = flick.mapToItem(nav.root,
            horizontal ? Number(localDelta) : 0,
            horizontal ? 0 : Number(localDelta))
        return horizontal ? Math.abs(after.x - before.x) : Math.abs(after.y - before.y)
    }

    function _localDistanceForRootBudget(flick, horizontal, rootDistance) {
        var unit = nav._rootDistanceForLocalDelta(flick, horizontal, 1)
        return unit > 0.000001 ? Number(rootDistance) / unit : Number(rootDistance)
    }

    function _hasUnsupportedTransform(item) {
        for (var node = item; node; node = node.parent) {
            if (Math.abs(Number(node.rotation || 0)) > 0.000001)
                return true
            var transforms = node.transform
            if (transforms && transforms.length !== undefined) {
                for (var i = 0; i < transforms.length; ++i) {
                    var transform = transforms[i]
                    if (transform && (Math.abs(Number(transform.angle || 0)) > 0.000001
                            || Math.abs(Number(transform.rotation || 0)) > 0.000001))
                        return true
                }
            }
            if (node === nav.root)
                break
        }
        return false
    }

    function _blockedNestedPath(fromItem, owner, target) {
        for (var node = fromItem; node && node !== owner; node = node.parent) {
            if (!Viewport.isFlickable(node))
                continue
            var controller = nav._scrollControllerFor(node)
            if (controller && controller.arrowScrolling === false
                    && (!target || Viewport.contains(node.contentItem, target)))
                return true
        }
        return false
    }

    function _planAuthorized(plan) {
        for (var i = 0; i < plan.length; ++i) {
            var owner = plan[i].flick
            var controller = nav._scrollControllerFor(owner)
            if (controller && controller.arrowScrolling === false)
                return false
        }
        return true
    }

    function _directionalStep(flick, horizontal, controller) {
        var extent = horizontal ? Number(flick.width) : Number(flick.height)
        var configured = controller && controller.lineStep !== undefined
            ? Number(controller.lineStep) : Number(nav.scrollStep)
        if (!isFinite(extent) || !isFinite(configured) || extent <= 0 || configured <= 0)
            return 0
        var localStep = Math.min(configured, extent * 0.25)
        return nav._rootDistanceForLocalDelta(flick, horizontal, localStep)
    }

    function _rect(item) {
        var p0 = item.mapToItem(nav.root, 0, 0)
        var p1 = item.mapToItem(nav.root, Number(item.width), Number(item.height))
        var left = Math.min(p0.x, p1.x)
        var right = Math.max(p0.x, p1.x)
        var top = Math.min(p0.y, p1.y)
        var bottom = Math.max(p0.y, p1.y)
        return {
            left: left, right: right, top: top, bottom: bottom,
            cx: (left + right) / 2, cy: (top + bottom) / 2
        }
    }

    function _metric(fromRect, candidateRect, key, stableIndex) {
        var primary = 0
        var cross = 0
        var laneOverlap = false

        if (key === Qt.Key_Down) {
            primary = candidateRect.cy - fromRect.cy
            cross = Math.abs(candidateRect.cx - fromRect.cx)
            laneOverlap = candidateRect.right >= fromRect.left
                && candidateRect.left <= fromRect.right
        } else if (key === Qt.Key_Up) {
            primary = fromRect.cy - candidateRect.cy
            cross = Math.abs(candidateRect.cx - fromRect.cx)
            laneOverlap = candidateRect.right >= fromRect.left
                && candidateRect.left <= fromRect.right
        } else if (key === Qt.Key_Right) {
            primary = candidateRect.cx - fromRect.cx
            cross = Math.abs(candidateRect.cy - fromRect.cy)
            laneOverlap = candidateRect.bottom >= fromRect.top
                && candidateRect.top <= fromRect.bottom
        } else if (key === Qt.Key_Left) {
            primary = fromRect.cx - candidateRect.cx
            cross = Math.abs(candidateRect.cy - fromRect.cy)
            laneOverlap = candidateRect.bottom >= fromRect.top
                && candidateRect.top <= fromRect.bottom
        }

        if (primary < nav.minimumPrimaryDistance)
            return null
        return {
            alignment: cross / Math.max(1, primary),
            lanePenalty: laneOverlap ? 0 : 1,
            primary: primary,
            cross: cross,
            stableIndex: stableIndex
        }
    }

    function _metricBefore(a, b) {
        if (!b)
            return true
        var epsilon = 0.000001
        if (Math.abs(a.alignment - b.alignment) > epsilon)
            return a.alignment < b.alignment
        if (a.lanePenalty !== b.lanePenalty)
            return a.lanePenalty < b.lanePenalty
        if (Math.abs(a.primary - b.primary) > epsilon)
            return a.primary < b.primary
        if (Math.abs(a.cross - b.cross) > epsilon)
            return a.cross < b.cross
        return a.stableIndex < b.stableIndex
    }

    function targetFrom(fromItem, key, scope, includeOffscreen) {
        if (!fromItem || !nav.root || !nav.isDirectionalKey(key))
            return null
        var items = []
        nav._appendFocusable(scope || nav.root, items, includeOffscreen === true)
        var fromRect = nav._rect(fromItem)
        // A viewport holding pure-scroll focus enters from the opposite edge,
        // rather than selecting relative to its large centre rectangle.
        if (Viewport.isFlickable(fromItem)) {
            if (key === Qt.Key_Down) fromRect.cy = fromRect.top
            if (key === Qt.Key_Up) fromRect.cy = fromRect.bottom
            if (key === Qt.Key_Right) fromRect.cx = fromRect.left
            if (key === Qt.Key_Left) fromRect.cx = fromRect.right
        }
        var bestItem = null
        var bestMetric = null
        for (var i = 0; i < items.length; i++) {
            var candidate = items[i]
            if (candidate === fromItem)
                continue
            if (!nav._candidateAllowed(candidate, includeOffscreen === true))
                continue
            if (includeOffscreen && Viewport.revealPlan(candidate, nav.root,
                    key === Qt.Key_Left || key === Qt.Key_Right) === null)
                continue
            var metric = nav._metric(fromRect, nav._rect(candidate), key, i)
            if (metric && nav._metricBefore(metric, bestMetric)) {
                bestMetric = metric
                bestItem = candidate
            }
        }
        return bestItem
    }

    function _candidateAllowed(candidate, includeOffscreen) {
        if (!candidate || nav._hasUnsupportedTransform(candidate))
            return false
        var owner = nav._flickableOwner(candidate)
        var controller = owner ? nav._scrollControllerFor(owner) : null
        if (controller && controller.arrowScrolling === false)
            return false
        return includeOffscreen ? nav._eligible(candidate) : nav._landingEligible(candidate)
    }

    function moveFrom(fromItem, key) {
        if (!nav.isDirectionalKey(key) || !fromItem || nav._isEditable(fromItem))
            return false
        if (nav._hasUnsupportedTransform(fromItem))
            return false
        var horizontal = key === Qt.Key_Left || key === Qt.Key_Right
        var forward = key === Qt.Key_Down || key === Qt.Key_Right
        var reason = (key === Qt.Key_Up || key === Qt.Key_Left)
            ? Qt.BacktabFocusReason : Qt.TabFocusReason
        if (nav._restoreSectionReturn(fromItem, key, reason))
            return true
        var ownedViewport = false
        var blockedViewport = false
        // Exhaust each owning viewport before exporting into unrelated chrome.
        // Indexed collections still consume their arrows before this handler.
        for (var owner = fromItem; owner; owner = owner.parent) {
            if (Viewport.isFlickable(owner) && Viewport.contains(nav.root, owner)) {
                ownedViewport = true
                var controller = nav._scrollControllerFor(owner)
                if (controller && controller.arrowScrolling === false) {
                    blockedViewport = true
                    continue
                }
                var step = nav._directionalStep(owner, horizontal, controller)
                var local = nav.targetFrom(fromItem, key, owner.contentItem, false)
                if (!local)
                    local = nav.targetFrom(fromItem, key, owner.contentItem, true)
                if (local && nav._land(local, key, reason, step)) {
                    nav._rememberSectionTransition(fromItem, local)
                    return true
                }
                if (nav._blockedNestedPath(fromItem, owner, local))
                    continue
                if (step > 0 && Viewport.setPosition(owner, horizontal,
                        Viewport.position(owner, horizontal)
                        + ((key === Qt.Key_Up || key === Qt.Key_Left) ? -1 : 1)
                        * nav._localDistanceForRootBudget(owner, horizontal, step))) {
                    // The step is the complete budget for this event. A target that
                    // becomes visible after the write lands on the next key, avoiding
                    // a second reveal in the same transition.
                    if (!nav._centerVisibleThroughClips(fromItem))
                        owner.forceActiveFocus(reason)
                    return true
                }
            }
            if (owner === nav.root)
                break
        }
        if (blockedViewport)
            return false
        var target = nav.targetFrom(fromItem, key)
        if (!target)
            target = nav.targetFrom(fromItem, key, nav.root, true)
        if (!target) {
            nav.boundaryRequested(key, fromItem)
            return false
        }
        // A rejected transform is a disallowed operation, not an over-budget
        // reveal. Never fall through to the raw destination-owner write.
        if (nav._hasUnsupportedTransform(target))
            return false
        var targetOwner = nav._flickableOwner(target)
        if (targetOwner && Viewport.contains(nav.root, targetOwner)) {
            var targetController = nav._scrollControllerFor(targetOwner)
            if (targetController && targetController.arrowScrolling === false)
                return false
            var targetStep = nav._directionalStep(targetOwner, horizontal, targetController)
            if (!nav._land(target, key, reason, targetStep)) {
                if (targetStep > 0 && Viewport.setPosition(targetOwner, horizontal,
                        Viewport.position(targetOwner, horizontal)
                        + (forward ? 1 : -1)
                        * nav._localDistanceForRootBudget(targetOwner, horizontal, targetStep))) {
                    targetOwner.forceActiveFocus(reason)
                    return true
                }
                return false
            }
            nav._rememberSectionTransition(fromItem, target)
            return true
        }
        // A content viewport owns an exhausted directional boundary. Only an
        // explicit owner transition may export into unrelated chrome; the
        // default surface has no such transition.
        if (ownedViewport && key === Qt.Key_Down)
            return false
        return nav._land(target, key, reason, nav.scrollStep)
    }

    function _land(target, key, reason, maxDistance) {
        var horizontal = key === Qt.Key_Left || key === Qt.Key_Right
        if (maxDistance === undefined || !isFinite(Number(maxDistance)))
            maxDistance = nav.scrollStep
        if (nav._hasUnsupportedTransform(target))
            return false
        if (nav._coveredByHigherSibling(target))
            return false
        var plan = Viewport.revealPlan(target, nav.root, horizontal)
        if (plan === null)
            return false
        if (!nav._planAuthorized(plan))
            return false
        if (!Viewport.applyPlan(plan, horizontal, maxDistance))
            return false
        if (!nav._landingEligible(target))
            return false
        // Collection-managed rails keep focus on their Flickable owner while delegates
        // remain semantic selection faces. Land the owner after the visible target has
        // passed policy/geometry checks; do not ask a non-focusable delegate to own focus.
        var collectionOwner = nav._collectionFocusOwner(target)
        if (collectionOwner && nav._selectCollectionItem(collectionOwner, target)) {
            collectionOwner.forceActiveFocus(reason)
            return collectionOwner.activeFocus === true
        }
        target.forceActiveFocus(reason)
        return target.activeFocus === true
    }

    function move(key) {
        return nav.moveFrom(nav.activeItem(), key)
    }

    function handle(event) {
        if (!event || !nav.isDirectionalKey(event.key))
            return false
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return false
        nav.beginNavigation(event.key)
        if (!nav.move(event.key))
            return false
        event.accepted = true
        return true
    }
}
