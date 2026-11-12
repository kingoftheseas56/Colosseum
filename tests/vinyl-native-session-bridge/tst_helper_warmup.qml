import QtQuick
import QtTest
import Colosseum.Vinyl 1.0

TestCase {
    id: test
    name: "VinylHelperWarmActivation"
    when: windowShown
    property var session
    property var source
    Component { id: sessionFactory; VinylBackendSession {} }
    Component { id: warmSourceFactory; WarmTestSource {} }
    Component { id: oldSourceFactory; VideoTestSource {} }
    function init() {
        failOnWarning(/.*/)
        session = sessionFactory.createObject(test)
        source = warmSourceFactory.createObject(test)
    }
    function cleanup() {
        session.destroy()
        source.destroy()
        wait(0)
    }
    function test_source_before_profile() {
        session.source = source
        compare(source.warmCount, 0)
        session.profileScope = "local-test"
        compare(source.warmCount, 1)
        session.profileScope = "local-test"
        session.source = source
        compare(source.warmCount, 1)
        session.clearProfile()
        verify(source.invalidationCount > 0)
    }
    function test_same_profile_reactivation_warms_once() {
        session.source = source
        session.profileScope = "local-test"
        compare(source.warmCount, 1)
        session.clearProfile()
        session.profileScope = "local-test"
        compare(source.warmCount, 2)
        session.profileScope = "local-test"
        compare(source.warmCount, 2)
    }
    function test_profile_before_source() {
        session.profileScope = "local-test"
        compare(source.warmCount, 0)
        session.source = source
        compare(source.warmCount, 1)
        session.shutdown()
        verify(source.invalidationCount > 0)
    }
    function test_invalid_profile_does_not_warm() {
        session.profileScope = ""
        session.source = source
        compare(source.warmCount, 0)
    }
    function test_older_source_without_capability_has_no_warning() {
        session.profileScope = "local-test"
        var older = oldSourceFactory.createObject(test)
        session.source = older
        session.shutdown()
        session.source = null
        older.destroy()
        wait(0)
        compare(source.warmCount, 0)
    }
}
