.pragma library

function single() {
    return {
        id: "hail-mary", title: "Project Hail Mary", authors: [{ name: "Andy Weir" }],
        description: "Ryland Grace wakes aboard a distant spacecraft, alone and unable to remember his mission. As his memories return, he discovers that the survival of Earth depends on solving a seemingly impossible scientific mystery.",
        recordings: [{ id: "phm-audio", recordingKey: "rec-phm", narrators: [{ name: "Ray Porter" }],
                       language: "English", abridgement: "unabridged", runtimeMin: 970 }],
        series: []
    };
}

function series() {
    return {
        id: "way-of-kings", title: "The Way of Kings", authors: [{ name: "Brandon Sanderson" }],
        description: "On storm-scoured Roshar, Kaladin fights to survive a brutal bridge crew. Dalinar’s visions and Shallan’s dangerous research point toward the return of ancient powers.",
        recordings: [{ id: "wok-audio", recordingKey: "rec-wok", narrators: [{ name: "Kate Reading" }, { name: "Michael Kramer" }],
                       language: "English", abridgement: "unabridged", runtimeMin: 2730 }],
        series: [{ id: "stormlight", name: "The Stormlight Archive", numberText: "1", positionSort: 1 }]
    };
}

function seriesWorks() {
    // Unsorted: ordering uses position, never integer display numbers.
    return [
        { id: "wind-truth", title: "Wind and Truth", number_text: "5", position_sort: 7 },
        { id: "edgedancer", title: "Edgedancer", number_text: "2.5", position_sort: 3 },
        { id: "way-of-kings", title: "The Way of Kings", number_text: "1", position_sort: 1 },
        { id: "rhythm-war", title: "Rhythm of War", number_text: "4", position_sort: 6 },
        { id: "dawnshard", title: "Dawnshard", number_text: "3.5", position_sort: 5 },
        { id: "words-radiance", title: "Words of Radiance", number_text: "2", position_sort: 2 },
        { id: "oathbringer", title: "Oathbringer", number_text: "3", position_sort: 4 }
    ];
}
