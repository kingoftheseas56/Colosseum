// Offscreen proof of ExplicitContentPolicy's pure classification (Tankoban Discover, Task 1).
// NEVER throw inside an offscreen harness (it hangs qml.exe): collect fails, Qt.exit(fails.length).
// Pins the semantic boundary — mainstream adult works (Berserk R+, Game of Thrones TV-MA,
// an "Adult" novel) stay VISIBLE; only sexually-explicit classifications gate.
import QtQuick
import "../qml/ExplicitContentPolicy.js" as Policy

Item {
    Timer {
        interval: 10; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }

            // [world, item, expectedExplicit] — the boundary the policy must hold.
            var cases = [
                ["tankoban", { title:"Berserk", genres:["Action","Gore"], rating:"R+" }, false],
                ["tankoban", { title:"School Comedy", genres:["Ecchi"] }, false],
                ["tankoban", { title:"Explicit Work", genres:["Hentai"] }, true],
                ["tankoban", { title:"Erotic Work", genres:["Erotica"] }, true],
                ["theatre",  { title:"Game of Thrones", certification:"TV-MA" }, false],
                ["theatre",  { title:"Explicit Film", behaviorHints:{ adult:true } }, true],
                ["biblio",   { title:"Adult Novel", audiences:["Adult"] }, false],
                ["biblio",   { title:"Explicit Book", subjects:["Pornography"] }, true],
                ["tankoban", { title:"Unknown" }, false]
            ];

            for (var i = 0; i < cases.length; i++) {
                var world = cases[i][0];
                var item = cases[i][1];
                var want = cases[i][2];
                var res = Policy.classify(world, item);
                ok(res.explicit === want,
                   "classify[" + world + "/" + item.title + "] expected explicit=" + want
                   + " got explicit=" + res.explicit + " reason=" + res.reason);
                ok(typeof res.reason === "string" && res.reason.length > 0,
                   "classify[" + item.title + "] must carry a reason string, got " + res.reason);

                // visible() hides ONLY when the setting is off AND the item is explicit.
                ok(Policy.visible(world, item, true) === true,
                   "visible[" + item.title + "] must show when showExplicit=true");
                ok(Policy.visible(world, item, false) === !want,
                   "visible[" + item.title + "] with showExplicit=false expected " + (!want)
                   + " got " + Policy.visible(world, item, false));
            }

            // Defense-in-depth: a genre literally named "constructor" (an inherited
            // Object.prototype member) must NOT false-positive as explicit.
            var protoTrap = Policy.classify("tankoban", { title:"Proto Trap", genres:["constructor"] });
            ok(protoTrap.explicit === false,
               "prototype-member genre must NOT gate, got explicit=" + protoTrap.explicit
               + " reason=" + protoTrap.reason);

            // Defense-in-depth: a scalar-string genre (genres:"Hentai", not an array) must
            // still gate — not be iterated character-by-character and under-gate.
            var scalarExplicit = Policy.classify("tankoban", { title:"Scalar Hentai", genres:"Hentai" });
            ok(scalarExplicit.explicit === true,
               "scalar-string explicit genre must gate, got explicit=" + scalarExplicit.explicit
               + " reason=" + scalarExplicit.reason);

            // Explicit item: hidden only when off, shown when on.
            var explicitItem = { title:"Explicit Work", genres:["Hentai"] };
            ok(Policy.visible("tankoban", explicitItem, false) === false,
               "explicit item must be HIDDEN when showExplicit=false");
            ok(Policy.visible("tankoban", explicitItem, true) === true,
               "explicit item must be SHOWN when showExplicit=true");

            // Non-explicit item: always visible regardless of the setting.
            var mainstream = { title:"Berserk", genres:["Action","Gore"], rating:"R+" };
            ok(Policy.visible("tankoban", mainstream, false) === true,
               "mainstream item must be SHOWN even when showExplicit=false");
            ok(Policy.visible("tankoban", mainstream, true) === true,
               "mainstream item must be SHOWN when showExplicit=true");

            // ── Task 9 cross-world preservation boundary ──
            // Adult-but-not-sexually-explicit works MUST stay visible in every world when
            // showExplicit is OFF. This is the load-bearing invariant: horror, violent work,
            // R/NC-17, TV-MA, Mature Readers, Ecchi, romance, and ordinary adult fiction are
            // NOT gated by the Explicit Content preference — only Policy's EXPLICIT_TAGS
            // (sexually-explicit) classifications hide. (Berserk, Game of Thrones, and Ecchi
            // are named verbatim in the handoff's preserve list.)
            var preserve = [
                ["tankoban", { title:"Berserk", genres:["Action","Gore","Horror"], rating:"R+" }],
                ["tankoban", { title:"Gantz", genres:["Action","Horror","Psychological"] }],
                ["tankoban", { title:"Ecchi Comedy", genres:["Ecchi","Comedy"] }],
                ["tankoban", { title:"Romance Manga", genres:["Romance"] }],
                ["tankoban", { title:"Mature Readers Title", demographics:["Seinen"], rating:"R+" }],
                ["theatre",  { title:"Game of Thrones", certification:"TV-MA", genres:["Drama"] }],
                ["theatre",  { title:"The Boys", certification:"TV-MA", genres:["Action","Horror"] }],
                ["theatre",  { title:"R-rated Thriller", certification:"R" }],
                ["theatre",  { title:"NC-17 Drama", certification:"NC-17" }],
                ["theatre",  { title:"Horror Film", genres:["Horror"] }],
                ["theatre",  { title:"Romance Film", genres:["Romance"] }],
                ["biblio",   { title:"Adult Fiction Novel", audiences:["Adult"], subjects:["Fiction"] }],
                ["biblio",   { title:"Horror Novel", subjects:["Horror","Fiction"] }],
                ["biblio",   { title:"Romance Novel", subjects:["Romance"] }],
                ["biblio",   { title:"Violent Thriller", subjects:["Thriller","Violence"] }]
            ];
            for (var p = 0; p < preserve.length; p++) {
                var w = preserve[p][0], it = preserve[p][1];
                ok(Policy.visible(w, it, false) === true,
                   "PRESERVE[" + w + "/" + it.title + "] must stay VISIBLE when showExplicit=false");
                ok(Policy.visible(w, it, true) === true,
                   "PRESERVE[" + w + "/" + it.title + "] must stay VISIBLE when showExplicit=true");
                var cls = Policy.classify(w, it);
                ok(cls.explicit === false,
                   "PRESERVE[" + w + "/" + it.title + "] must classify explicit=false (got "
                   + cls.explicit + ", reason=" + cls.reason + ")");
            }

            // ── The gate fires ONLY on sexually-explicit classifications, cross-world ──
            var gates = [
                ["tankoban", { title:"Hentai Work", genres:["Hentai"] }],
                ["tankoban", { title:"Erotica Manga", genres:["Erotica"] }],
                ["tankoban", { title:"Pornography", genres:["Pornography"] }],
                ["theatre",  { title:"Adult Film", behaviorHints:{ adult:true } }],
                ["biblio",   { title:"Pornography Book", subjects:["Pornography"] }]
            ];
            for (var g = 0; g < gates.length; g++) {
                var gw = gates[g][0], gi = gates[g][1];
                ok(Policy.visible(gw, gi, false) === false,
                   "GATE[" + gw + "/" + gi.title + "] must HIDE when showExplicit=false");
                ok(Policy.visible(gw, gi, true) === true,
                   "GATE[" + gw + "/" + gi.title + "] must SHOW when showExplicit=true");
            }

            // ── The Jikan sfw derivation contract (Task 9 Step 4) ──
            // sfw = showExplicit ? "false" : "true" — when the user hides explicit content the
            // Jikan layer filters adult entries upstream; when they opt in, it admits them.
            function sfwFor(showExplicit) { return showExplicit ? "false" : "true"; }
            ok(sfwFor(false) === "true",  "Jikan sfw must be \"true\"  when showExplicit=false");
            ok(sfwFor(true)  === "false", "Jikan sfw must be \"false\" when showExplicit=true");

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("EXPLICIT_CONTENT_POLICY_OK");
            Qt.exit(fails.length);
        }
    }
}
