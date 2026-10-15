// tst_vault_admission_api — vault-admission slice. Proves VaultApi.continueRail gates the Vault
// Continue list on an EXACT durable "Admitted" verdict and caps AFTER filtering: rejected, unprobed,
// and catalogue rows never enter, and never consume the requested output cap. Pure JS over a fake
// Progress; the admission map is injected as the third argument (the VaultLibrary.admissionById
// projection in production). GUILESS logic test.
import QtQuick
import QtTest
import "../../qml/VaultApi.js" as VaultApi

TestCase {
    name: "VaultAdmissionApi"

    function fakeProgress(rows) {
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

    function test_continue_requires_admitted_verdict_and_caps_after_filter() {
        var p = fakeProgress([
            { id: "catalogue:x", kind: "video", title: "Catalog", progress: 0.4,
              resume: { localPath: "C:/catalog.mp4" } },
            { id: "vault:rejected", kind: "video", title: "Rejected", progress: 0.5,
              resume: { localPath: "C:/rejected.mp4" } },
            { id: "vault:unknown", kind: "video", title: "Unknown", progress: 0.6,
              resume: { localPath: "C:/unknown.mp4" } },
            { id: "vault:a", kind: "video", title: "A", progress: 0.7,
              resume: { localPath: "C:/a.mp4" } },
            { id: "vault:b", kind: "video", title: "B", progress: 0.8,
              resume: { localPath: "C:/b.mp4" } }
        ])

        var verdicts = {
            "vault:rejected": "RejectedNoVideo",
            "vault:a": "Admitted",
            "vault:b": "Admitted"
        }

        var out = VaultApi.continueRail(p, 1, verdicts)
        compare(out.length, 1)
        compare(out[0].id, "vault:a")
        compare(out[0].path, "C:/a.mp4")

        // Negative controls: absent or rejected admission never promotes.
        compare(VaultApi.continueRail(p, 10, {}).length, 0)
        compare(VaultApi.continueRail(
                    p, 10, { "vault:rejected": "RejectedError" }).length, 0)
    }
}
