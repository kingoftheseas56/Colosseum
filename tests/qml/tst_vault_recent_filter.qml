// tst_vault_recent_filter — vault-admission slice (§9). Proves VaultApi.recentWithoutVault strips
// vault: rows from a catalogue recents read BEFORE the hard cap, so a run of local items can never
// shrink a catalogue rail. The negative control reproduces the old filter-after-limit trap collapsing
// the rail to zero. Pure JS over a fake Progress; GUILESS logic test.
import QtQuick
import QtTest
import "../../qml/VaultApi.js" as VaultApi

TestCase {
    name: "VaultRecentFilter"

    function progressWith(rows) {
        return {
            rows: rows,
            recent: function(kind, limit) {
                var filtered = []
                for (var i = 0; i < this.rows.length; ++i) {
                    if (!kind || this.rows[i].kind === kind)
                        filtered.push(this.rows[i])
                }
                return (limit && limit > 0)
                        ? filtered.slice(0, limit)
                        : filtered
            }
        }
    }

    function test_filter_happens_before_limit() {
        var rows = []
        for (var i = 0; i < 12; ++i)
            rows.push({ id: "vault:" + i, kind: "video" })
        for (var j = 0; j < 12; ++j)
            rows.push({ id: "catalogue:" + j, kind: "video" })

        var p = progressWith(rows)

        // Negative control: old filter-after-limit shape collapses the rail.
        var naive = p.recent("video", 12).filter(function(e) {
            return !VaultApi.isVault(e.id)
        })
        compare(naive.length, 0)

        var fixed = VaultApi.recentWithoutVault(p, "video", 12)
        compare(fixed.length, 12)
        compare(fixed[0].id, "catalogue:0")
        compare(fixed[11].id, "catalogue:11")
    }

    function test_unbounded_preserves_all_non_vault_rows() {
        var p = progressWith([
            { id: "vault:v", kind: "video" },
            { id: "catalogue:a", kind: "video" },
            { id: "catalogue:b", kind: "video" }
        ])
        var fixed = VaultApi.recentWithoutVault(p, "video", 0)
        compare(fixed.length, 2)
        compare(fixed[0].id, "catalogue:a")
        compare(fixed[1].id, "catalogue:b")
    }
}
