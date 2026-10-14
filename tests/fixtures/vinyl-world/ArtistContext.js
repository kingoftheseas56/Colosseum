.pragma library

// Short authored summaries, checked against the linked sources on 2026-10-14.
// Enrichment is optional and separate from catalogue identity and playback.
var data = {
    "Eminem": {
        roles: "Rapper · Songwriter · Producer",
        blurb: "Detroit battle rap, intricate rhyme and larger-than-life alter egos. Marshall Mathers turns autobiography, dark humour and pop hooks into a world of his own.",
        history: "After Infinite and the Slim Shady EP, Eminem joined Dr. Dre's Aftermath label. The Slim Shady LP brought his breakthrough in 1999; The Marshall Mathers LP and The Eminem Show followed. He starred in 8 Mile in 2002. Relapse and Recovery marked his return after a hiatus, while later records revisited the characters and conflicts running through his work.",
        facts: [{label:"Name",value:"Marshall Bruce Mathers III"},{label:"Origin",value:"Detroit, Michigan"},{label:"Sound",value:"Hip-hop"},{label:"Also known as",value:"Slim Shady"}],
        sources: [{title:"Wikipedia",url:"https://en.wikipedia.org/wiki/Eminem"},{title:"Wikidata",url:"https://www.wikidata.org/wiki/Q5608"}]
    },
    "Ilaiyaraaja": {
        roles: "Composer · Singer · Music director",
        blurb: "Tamil folk traditions, Carnatic melody and Western orchestration meet in a body of work that moves between film songs, instrumental music and the concert hall.",
        history: "Ilaiyaraaja grew up in Pannaipuram and performed with his brother's travelling troupe before working in Chennai as a musician and arranger. Annakili marked his film-composing debut in 1976. Beyond cinema, How to Name It? and Nothing But Wind explored the meeting of Indian and Western classical traditions.",
        facts: [{label:"From",value:"Pannaipuram, India"},{label:"Career began",value:"1976"},{label:"Fields",value:"Film music · Composition · Singing"}],
        sources: [{title:"Wikipedia",url:"https://en.wikipedia.org/wiki/Ilaiyaraaja"},{title:"Wikidata",url:"https://www.wikidata.org/wiki/Q2720141"}],
        singlesArtist: "Ilaiyaraaja"
    },
    "Michael Jackson": {
        roles: "Singer · Songwriter · Dancer",
        blurb: "Pop, soul, funk and rock, carried by a voice and a visual language that made the music video part of the song's identity.",
        history: "Jackson began performing with the Jackson 5 before building a solo career. Off the Wall expanded his adult sound in 1979; Thriller followed in 1982, pairing songs with ambitious music videos. Bad continued that run in 1987.",
        facts: [{label:"Fields",value:"Pop · Soul · Funk · Rock"}],
        sources: [{title:"Wikipedia",url:"https://en.wikipedia.org/wiki/Michael_Jackson"}]
    },
    "Joni Mitchell": {
        roles: "Singer-songwriter",
        blurb: "Songwriting at the centre: explore Blue, then follow the songs back into the artist's wider story.",
        history: "Blue was released in 1971, between Ladies of the Canyon and For the Roses. Mitchell's subsequent discography includes Court and Spark, The Hissing of Summer Lawns and Hejira.",
        facts: [],
        sources: [{title:"Wikipedia",url:"https://en.wikipedia.org/wiki/Joni_Mitchell"}]
    }
};

function forArtist(name) {
    return data[name] || {roles:"Artist",blurb:"",history:"",facts:[],sources:[]};
}
