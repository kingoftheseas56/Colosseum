import QtQuick
import QtTest
import "../../qml/vinyl/VinylNotePicker.js" as Picker

TestCase {
    name: "VinylNotePicker"

    function test_short_credible_beats_longer_verified() {
        compare(Picker.pick([
            { text: "Short note wins", votes: 12 },
            { text: "A verified note with several more words", votes: 1, verified: true }
        ], 30000).text, "Short note wins")
    }

    function test_shorter_fits_when_long_verified_does_not() {
        var shortNote = { text: "Brief annotation", votes: 10 }
        var longVerifiedNote = {
            text: "A verified annotation that cannot fit this window",
            votes: 1,
            verified: true
        }
        compare(Picker.pick([longVerifiedNote], 1500), null)
        compare(Picker.pick([shortNote, longVerifiedNote], 1500).text, "Brief annotation")
    }

    function test_verified_with_low_votes_is_eligible() {
        compare(Picker.pick([
            { text: "Verified note", votes: 0, verified: true }
        ], 30000).text, "Verified note")
    }

    function test_low_vote_unverified_is_excluded() {
        compare(Picker.pick([
            { text: "Not enough votes", votes: 9 },
            { text: "Votes must be numeric", votes: "100" },
            { text: "Votes must be finite", votes: Infinity }
        ], 30000), null)
    }

    function test_empty_and_malformed_bodies_are_excluded() {
        compare(Picker.pick([
            { text: "", votes: 100 },
            { text: "  \n\t  ", verified: true },
            { text: 42, verified: true },
            { text: { body: "not a note string" }, votes: 100 },
            { text: ["not", "a", "note"], verified: true }
        ], 30000), null)
    }

    function test_nonfinite_and_nonpositive_windows() {
        var notes = [{ text: "A credible note", votes: 10 }]
        compare(Picker.pick(notes, Infinity), null)
        compare(Picker.pick(notes, NaN), null)
        compare(Picker.pick(notes, 0), null)
        compare(Picker.pick(notes, -1), null)
        compare(Picker.pick(notes, "30000"), null)
    }

    function test_shortest_and_stable_input_order() {
        var notes = [
            { text: "Long notes are longer than either short note", votes: 20 },
            { text: "First short note", votes: 10 },
            { text: "Other short note", verified: true, votes: 0 }
        ]
        compare(Picker.pick(notes, 30000).text, "First short note")
        compare(notes[0].text, "Long notes are longer than either short note")
        compare(notes[1].text, "First short note")
        compare(notes[2].text, "Other short note")
    }

    function test_exact_reading_budget_boundary() {
        var note = { text: "One two three", votes: 10 }
        var exactBudget = 3 * 60000 / Picker.WORDS_PER_MINUTE + Picker.READING_LEAD_MS
        compare(Picker.pick([note], exactBudget).text, "One two three")
        compare(Picker.pick([note], exactBudget - 0.001), null)
    }

    function test_empty_notes() {
        compare(Picker.pick([], 30000), null)
    }

    function test_autoTurnsLongNasLikeSourceIntoFaithfulShortWindowExcerpt() {
        var sourceText = "The verse frames a young narrator looking back at a childhood shaped by scarcity and danger while measuring the distance between survival and ambition. Each street detail makes the setting feel lived-in rather than mythic, and the closing reflection connects personal memory with the neighborhood's shared history and future."
        compare(Picker.wordCount(sourceText), 49)
        var attribution = { author: "community", source: "SOURCE" }
        var note = {
            id: "nas-note",
            artistId: "artist-nas",
            text: sourceText,
            votes: 149,
            verified: false,
            source: "SOURCE",
            attribution: attribution
        }

        var notes = [note]
        var result = Picker.pickAuto(notes, 1000)
        verify(result !== null)
        verify(result !== note)
        compare(result.text, "The verse frames a young narrator looking back at a childhood shaped by scarcity and\u2026")
        compare(Picker.wordCount(result.text), 15)
        compare(result.isExcerpt, true)
        compare(result.autoReadMs, 5000)
        compare(result.id, note.id)
        compare(result.artistId, note.artistId)
        compare(result.votes, 149)
        compare(result.verified, false)
        verify(result.attribution === attribution)
        compare(note.text, sourceText)
        compare(notes.length, 1)
        verify(notes[0] === note)
        verify(typeof note.isExcerpt === "undefined")
        verify(typeof note.autoReadMs === "undefined")
    }

    function test_autoKeepsShortBodyWithoutEllipsisAndAppliesReadingLead() {
        var note = { text: "A short sourced note.", votes: 10 }
        var result = Picker.pickAuto([note], 1000)
        compare(result.text, note.text)
        compare(result.isExcerpt, false)
        compare(result.autoReadMs, 5000)
    }

    function test_autoPrefersCompleteFirstParagraphThatFits() {
        var firstParagraph = "The opening image establishes a quiet contrast. It also hints at later grief."
        var note = {
            text: firstParagraph + "\n\nA second paragraph continues the explanation beyond the short automatic reading window.",
            votes: 10
        }
        var result = Picker.pickAuto([note], 1000)
        compare(result.text, firstParagraph + "\u2026")
        compare(Picker.wordCount(result.text), 13)
        compare(result.isExcerpt, true)
    }

    function test_autoPrefersCompleteFirstSentenceThatFits() {
        var firstSentence = "The passage contrasts public confidence with private uncertainty across the song's opening verse."
        var note = {
            text: firstSentence + " A later section tracks how family history alters the narrator's choices and hopes.",
            votes: 10
        }
        var result = Picker.pickAuto([note], 1000)
        compare(result.text, firstSentence + "\u2026")
        compare(Picker.wordCount(result.text), 13)
    }

    function test_autoUsesClauseNearCapBeforeCuttingAtWordBoundary() {
        var note = {
            text: "The note offers a thoughtful explanation, tracing ambition, memory, family, loss, and resilience through the passage while showing how each decision shapes the narrator's future.",
            votes: 10
        }
        var result = Picker.pickAuto([note], 1000)
        compare(result.text, "The note offers a thoughtful explanation, tracing ambition, memory, family, loss,\u2026")
        compare(Picker.wordCount(result.text), 11)
    }

    function test_autoPrefersShortestSupportedFullBodyOverLongerVerifiedBody() {
        var supported = { id: "short-supported", text: "A short supported source note", votes: 10 }
        var verified = {
            id: "long-verified",
            text: "A verified source note with enough detail to be much longer than the supported note",
            votes: 1,
            verified: true
        }
        compare(Picker.pickAuto([verified, supported], 1000).id, supported.id)
    }

    function test_autoPreservesInputOrderForEqualLengthBodies() {
        var first = { id: "first", text: "A short source note", votes: 10 }
        var second = { id: "second", text: "A brief source note", verified: true }
        compare(Picker.wordCount(first.text), Picker.wordCount(second.text))
        compare(Picker.pickAuto([first, second], 1000).id, first.id)
    }

    function test_autoLongWindowStillUsesTheTwentyFourWordCap() {
        var words = []
        for (var i = 1; i <= 30; ++i) words.push("word" + i)
        var note = { text: words.join(" "), votes: 10 }
        var result = Picker.pickAuto([note], 120000)
        compare(Picker.wordCount(result.text), 24)
        compare(result.text, words.slice(0, 24).join(" ") + "\u2026")
        verify(result.isExcerpt)
        verify(result.autoReadMs >= 5000 && result.autoReadMs <= 7500)
        compare(result.autoReadMs, 7358)
    }

    function test_autoIgnoresUrlOnlyAndMalformedBodies() {
        compare(Picker.pickAuto([
            { text: "https://example.org/source?id=7", verified: true },
            { text: "www.example.org/annotation", votes: 100 },
            { text: "example.org/source", votes: 100 }
        ], 1000), null)
        compare(Picker.pickAuto([
            null,
            42,
            { text: "", votes: 100 },
            { text: " \n\t ", verified: true },
            { text: 42, verified: true },
            { text: ["not", "a", "string"], votes: 100 }
        ], 1000), null)
        compare(Picker.pickAuto({ length: 1, 0: { text: "Not an array", votes: 10 } }, 1000), null)
    }

    function test_autoDoesNotReturnAnAbbreviationAsTheExplanation() {
        compare(Picker.pickAuto([
            { text: "Dr.", verified: true },
            { text: "U.S.", votes: 10 },
            { text: "Dr. Dr.", votes: 10 }
        ], 1000), null)
    }

    function test_autoRejectsUnknownNonfiniteAndNonpositiveWindows() {
        var notes = [{ text: "A credible source note", votes: 10 }]
        compare(Picker.pickAuto(notes, undefined), null)
        compare(Picker.pickAuto(notes, null), null)
        compare(Picker.pickAuto(notes, NaN), null)
        compare(Picker.pickAuto(notes, Infinity), null)
        compare(Picker.pickAuto(notes, 0), null)
        compare(Picker.pickAuto(notes, -1), null)
        compare(Picker.pickAuto(notes, "1000"), null)
    }

    function test_autoRejectsUntrustedVoteAndVerificationTypes() {
        compare(Picker.pickAuto([
            { text: "String votes do not qualify", votes: "100" },
            { text: "Boolean votes do not qualify", votes: true },
            { text: "NaN votes do not qualify", votes: NaN },
            { text: "Infinite votes do not qualify", votes: Infinity },
            { text: "Verification must be boolean true", verified: 1 }
        ], 1000), null)
    }

    function test_autoKeepsNonAsciiSourceWordsAsVerbatimPrefix() {
        var sourceText = "Café stories éclairent chaque passage tandis que mémoire familiale relie les rues aux rêves du narrateur, et la dernière image rappelle Tokyo sans changer de sens."
        var note = { text: sourceText, votes: 10, language: "fr" }
        var result = Picker.pickAuto([note], 1000)
        compare(result.text, "Café stories éclairent chaque passage tandis que mémoire familiale relie les rues aux rêves du\u2026")
        verify(sourceText.indexOf(result.text.substring(0, result.text.length - 1)) === 0)
        verify(result.text.indexOf("Tokyo") === -1)
        compare(result.language, "fr")
        compare(note.text, sourceText)
    }
}
