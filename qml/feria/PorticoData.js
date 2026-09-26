.pragma library
// Sample data lifted from the adjacent HTML prototype. Listings and availability are illustrative.

var P = {
  netflix:{n:"Netflix",v:"watch",d:"netflix.com",tp:"netflix.com/title/‹id›",sp:"netflix.com/search?q="},
  prime:{n:"Prime Video",v:"watch",d:"primevideo.com",tp:"primevideo.com/detail/‹id›",sp:"primevideo.com/search?phrase="},
  hbomax:{n:"HBO Max",v:"watch",d:"hbomax.com",tp:"play.hbomax.com/‹type›/‹id›",sp:"play.hbomax.com/search?q="},
  disney:{n:"Disney+",v:"watch",d:"disneyplus.com",tp:"disneyplus.com/browse/entity-‹id›",sp:"disneyplus.com/search?q="},
  appletv:{n:"Apple TV",v:"watch",d:"tv.apple.com",tp:"tv.apple.com/‹type›/‹slug›/‹id›",sp:"tv.apple.com/search?term="},
  crunchyroll:{n:"Crunchyroll",v:"watch",d:"crunchyroll.com",tp:"crunchyroll.com/series/‹id›",sp:"crunchyroll.com/search?q="},
  youtube:{n:"YouTube",v:"watch",d:"youtube.com",tp:"youtube.com/watch?v=‹id›",sp:"youtube.com/results?search_query="},
  hulu:{n:"Hulu",v:"watch",d:"hulu.com",tp:"hulu.com/‹type›/‹slug›",sp:"hulu.com/search?q="},
  mubi:{n:"MUBI",v:"watch",d:"mubi.com",tp:"mubi.com/films/‹slug›",sp:"mubi.com/search/films?query="},
  spotify:{n:"Spotify",v:"listen",d:"open.spotify.com",tp:"open.spotify.com/‹type›/‹id›",sp:"open.spotify.com/search/"},
  ytmusic:{n:"YouTube Music",v:"listen",d:"music.youtube.com",tp:"music.youtube.com/browse/‹id›",sp:"music.youtube.com/search?q="},
  applemusic:{n:"Apple Music",v:"listen",d:"music.apple.com",tp:"music.apple.com/‹type›/‹id›",sp:"music.apple.com/search?term="},
  kindle:{n:"Kindle",v:"read",d:"read.amazon.com",tp:"read.amazon.com/?asin=‹asin›",sp:"amazon.com/s?i=digital-text&k="},
  playbooks:{n:"Google Play Books",v:"read",d:"play.google.com/books",tp:"play.google.com/store/books/details?id=‹id›",sp:"play.google.com/store/search?c=books&q="},
  mangaplus:{n:"MANGA Plus",v:"read",d:"mangaplus.shueisha.co.jp",tp:"mangaplus.shueisha.co.jp/titles/‹id›",sp:"mangaplus.shueisha.co.jp/search_result?keyword="},
  viz:{n:"VIZ",v:"read",d:"viz.com",tp:"viz.com/shonenjump/chapters/‹slug›",sp:"viz.com/search?search="},
  webtoon:{n:"WEBTOON",v:"read",d:"webtoons.com",tp:"webtoons.com/en/‹genre›/‹slug›/list?title_no=‹id›",sp:"webtoons.com/en/search?keyword="},
  dcui:{n:"DC Universe Infinite",v:"read",d:"dcuniverseinfinite.com",tp:"dcuniverseinfinite.com/comics/series/‹slug›",sp:"dcuniverseinfinite.com/search?q="},
  marvel:{n:"Marvel Unlimited",v:"read",d:"marvel.com/unlimited",tp:"read.marvel.com/#/series/‹id›",sp:"marvel.com/search?query="},
  kobo:{n:"Kobo",v:"read",app:true},
  applebooks:{n:"Apple Books",v:"read",app:true},
};

var CATALOG = { watch:["netflix","prime","hbomax","disney","appletv","crunchyroll","youtube","hulu","mubi"], listen:["spotify","ytmusic","applemusic"], read:["kindle","playbooks","mangaplus","viz","webtoon","dcui","marvel","kobo","applebooks"] };

var DEFAULT_APPS = ["netflix","prime","hbomax","disney","appletv","crunchyroll","youtube","spotify","ytmusic","kindle","mangaplus","webtoon","dcui"];

var T = {
 // Netflix
 stranger:{k:"series",t:"Stranger Things",y:"2016–2025",by:"The Duffer Brothers",f:["5 seasons","Science fiction","Horror"],imdb:"tt4574334",s:"In 1980s Hawkins, a missing boy pulls his friends into a war with a hidden dimension and the lab that opened it.",d:[["netflix","t"]]},
 wednesday:{k:"series",t:"Wednesday",y:"2022–",by:"Alfred Gough, Miles Millar",f:["2 seasons","Mystery","Comedy"],imdb:"tt13443470",s:"Wednesday Addams investigates a string of killings while enrolled at Nevermore Academy.",d:[["netflix","t"]]},
 squid:{k:"series",t:"Squid Game",y:"2021–2025",by:"Hwang Dong-hyuk",f:["3 seasons","Thriller"],imdb:"tt10919420",s:"Hundreds of debt-ridden players compete in lethal versions of children's games for a vast cash prize.",d:[["netflix","t"]]},
 arcane:{k:"series",t:"Arcane",y:"2021–2024",by:"Fortiche, Riot Games",f:["2 seasons","Animation","Fantasy"],imdb:"tt11126994",s:"Two sisters end up on opposite sides of a war between gleaming Piltover and the undercity of Zaun.",d:[["netflix","t"]]},
 oplive:{k:"series",t:"One Piece",y:"2023–",by:"Matt Owens, Steven Maeda",f:["Live action","2 seasons","Adventure"],imdb:"tt11737520",s:"A live-action retelling of Luffy's first voyages across the East Blue.",d:[["netflix","t"]],x:["opmanga","opanime"]},
 glassonion:{k:"film",t:"Glass Onion",y:"2022",by:"Rian Johnson",f:["2h 19m","Mystery","Comedy"],imdb:"tt11564570",s:"Benoit Blanc joins a tech billionaire's murder-mystery weekend on a Greek island, and a real murder follows.",d:[["netflix","t"]]},
 roma:{k:"film",t:"Roma",y:"2018",by:"Alfonso Cuarón",f:["2h 15m","Drama"],imdb:"tt6155172",s:"A year in the life of a live-in housekeeper for a middle-class family in 1970s Mexico City.",d:[["netflix","t"]]},
 vinland:{k:"anime",t:"Vinland Saga",y:"2019–2023",by:"Wit Studio, MAPPA",f:["2 seasons","Historical","Action"],imdb:"tt10233448",s:"A young warrior chases revenge through the Viking age, then searches for a land beyond war.",d:[["netflix","t"],["crunchyroll","t"]]},
 // Prime Video
 theboys:{k:"series",t:"The Boys",y:"2019–",by:"Eric Kripke",f:["4 seasons","Superhero","Satire"],imdb:"tt1190634",s:"A group of vigilantes sets out to take down corrupt superheroes who abuse their powers.",d:[["prime","t"]]},
 fallout:{k:"series",t:"Fallout",y:"2024–",by:"Jonathan Nolan, Lisa Joy",f:["1 season","Science fiction"],imdb:"tt12637874",s:"Two hundred years after the bombs, a vault dweller climbs up into the wasteland of Los Angeles.",d:[["prime","t"]]},
 reacher:{k:"series",t:"Reacher",y:"2022–",by:"Nick Santora",f:["3 seasons","Action","Crime"],imdb:"tt9288030",s:"A former military police investigator drifts into small-town trouble he refuses to walk away from.",d:[["prime","t"]]},
 invincible:{k:"series",t:"Invincible",y:"2021–",by:"Robert Kirkman",f:["3 seasons","Animation","Superhero"],imdb:"tt6741278",s:"The teenage son of Earth's most powerful hero learns what his father's legacy really means.",d:[["prime","t"]]},
 fleabag:{k:"series",t:"Fleabag",y:"2016–2019",by:"Phoebe Waller-Bridge",f:["2 seasons","Comedy","Drama"],imdb:"tt5687612",s:"A dry-witted woman navigates life and love in London while trying to cope with a tragedy.",d:[["prime","t"]]},
 pastlives:{k:"film",t:"Past Lives",y:"2023",by:"Celine Song",f:["1h 46m","Drama","Romance"],imdb:"tt13238346",s:"Two childhood friends from Seoul meet again in New York, decades after one of them emigrated.",d:[["prime","t"]]},
 eeaao:{k:"film",t:"Everything Everywhere All at Once",y:"2022",by:"Daniel Kwan, Daniel Scheinert",f:["2h 19m","Science fiction","Comedy"],imdb:"tt6710474",s:"A laundromat owner in the middle of a tax audit discovers she can borrow skills from her lives in other universes.",d:[["prime","t"],["mubi","s"]]},
 // HBO Max
 dune2:{k:"film",t:"Dune: Part Two",y:"2024",by:"Denis Villeneuve",f:["2h 46m","Science fiction"],imdb:"tt15239678",s:"Paul Atreides unites with the Fremen of Arrakis and wages war on the houses that destroyed his family.",d:[["hbomax","t"],["prime","s"]],x:["dunebook"]},
 lastofus:{k:"series",t:"The Last of Us",y:"2023–",by:"Craig Mazin, Neil Druckmann",f:["2 seasons","Drama","Horror"],imdb:"tt3581920",s:"Twenty years into a fungal pandemic, a smuggler escorts a teenage girl across a ruined America.",d:[["hbomax","t"]]},
 whitelotus:{k:"series",t:"The White Lotus",y:"2021–",by:"Mike White",f:["3 seasons","Satire","Drama"],imdb:"tt13406094",s:"Guests and staff at an exclusive resort unravel over one week of luxury and bad behaviour.",d:[["hbomax","t"]]},
 hotd:{k:"series",t:"House of the Dragon",y:"2022–",by:"Ryan Condal, George R. R. Martin",f:["2 seasons","Fantasy"],imdb:"tt11198330",s:"Two hundred years before Game of Thrones, House Targaryen tears itself apart over the Iron Throne.",d:[["hbomax","t"]]},
 penguin:{k:"series",t:"The Penguin",y:"2024",by:"Lauren LeFranc",f:["Limited series","Crime"],imdb:"tt15435876",s:"Oz Cobb makes his play for Gotham's underworld in the power vacuum after the Riddler's attack.",d:[["hbomax","t"]]},
 succession:{k:"series",t:"Succession",y:"2018–2023",by:"Jesse Armstrong",f:["4 seasons","Drama"],imdb:"tt7660850",s:"The Roy family fights over control of a global media empire as its founder's health fails.",d:[["hbomax","t"]]},
 spirited:{k:"film",t:"Spirited Away",y:"2001",by:"Hayao Miyazaki",f:["2h 5m","Animation","Fantasy"],imdb:"tt0245429",s:"A girl trapped in a world of spirits works in a bathhouse to free herself and her parents.",d:[["hbomax","t"]]},
 // Disney+
 andor:{k:"series",t:"Andor",y:"2022–2025",by:"Tony Gilroy",f:["2 seasons","Science fiction"],imdb:"tt9253284",s:"Cassian Andor goes from thief to rebel in the years before the Rebellion's first victory.",d:[["disney","t"]]},
 shogun:{k:"series",t:"Shōgun",y:"2024–",by:"Rachel Kondo, Justin Marks",f:["1 season","Historical drama"],imdb:"tt2788316",s:"In 1600 Japan, a shipwrecked English pilot is drawn into a warlord's struggle for power.",d:[["disney","t"],["hulu","t"]]},
 thebear:{k:"series",t:"The Bear",y:"2022–",by:"Christopher Storer",f:["4 seasons","Drama","Comedy"],imdb:"tt14452776",s:"A fine-dining chef comes home to run his late brother's Chicago sandwich shop.",d:[["disney","t"],["hulu","t"]]},
 loki:{k:"series",t:"Loki",y:"2021–2023",by:"Michael Waldron",f:["2 seasons","Superhero"],imdb:"tt9140554",s:"The god of mischief is pulled out of his timeline by an agency that polices the flow of time.",d:[["disney","t"]]},
 mando:{k:"series",t:"The Mandalorian",y:"2019–",by:"Jon Favreau",f:["3 seasons","Science fiction","Western"],imdb:"tt8111088",s:"A lone bounty hunter in the outer reaches of the galaxy takes on a child he was paid to deliver.",d:[["disney","t"]]},
 insideout2:{k:"film",t:"Inside Out 2",y:"2024",by:"Kelsey Mann",f:["1h 36m","Animation","Family"],imdb:"tt22022452",s:"New emotions move into Riley's head as she turns thirteen, and Joy's team is shown the door.",d:[["disney","t"]]},
 // Apple TV
 severance:{k:"series",t:"Severance",y:"2022–",by:"Dan Erickson",f:["2 seasons","Science fiction","Thriller"],imdb:"tt11280740",s:"Lumon employees undergo a procedure that splits their work memories from their private lives.",d:[["appletv","t"]]},
 slowhorses:{k:"series",t:"Slow Horses",y:"2022–",by:"After the novels by Mick Herron",f:["5 seasons","Spy","Drama"],imdb:"tt5875444",s:"Disgraced MI5 agents exiled to Slough House keep stumbling into the cases the service wants buried.",d:[["appletv","t"]]},
 tedlasso:{k:"series",t:"Ted Lasso",y:"2020–",by:"Jason Sudeikis, Bill Lawrence",f:["3 seasons","Comedy"],imdb:"tt10986410",s:"An American college football coach is hired to manage an English football club he knows nothing about.",d:[["appletv","t"]]},
 silo:{k:"series",t:"Silo",y:"2023–",by:"Graham Yost",f:["2 seasons","Science fiction"],imdb:"tt14688458",s:"Ten thousand people live in a buried silo, forbidden to ask why, until an engineer starts asking.",d:[["appletv","t"]]},
 pachinko:{k:"series",t:"Pachinko",y:"2022–2024",by:"Soo Hugh",f:["2 seasons","Drama"],imdb:"tt8888540",s:"Four generations of a Korean family chase survival and prosperity from Busan to Osaka to New York.",d:[["appletv","t"]]},
 foundation:{k:"series",t:"Foundation",y:"2021–",by:"David S. Goyer",f:["3 seasons","Science fiction"],imdb:"tt0804484",s:"A mathematician predicts the fall of a galactic empire and a band of exiles works to shorten the dark age.",d:[["appletv","t"]]},
 // Crunchyroll
 frieren:{k:"anime",t:"Frieren: Beyond Journey's End",y:"2023–",by:"Madhouse",f:["2 seasons","Fantasy","Adventure"],imdb:"tt22248376",s:"An elf mage who outlives her heroic party sets out to understand the humans she barely knew.",d:[["crunchyroll","t"],["netflix","s"]],x:["frierenmanga"]},
 jjk:{k:"anime",t:"Jujutsu Kaisen",y:"2020–",by:"MAPPA",f:["Ongoing","Action","Supernatural"],imdb:"tt12343534",s:"A high schooler swallows a cursed finger and joins a school of sorcerers who fight curses.",d:[["crunchyroll","t"],["netflix","s"]],x:["jjkmanga"]},
 chainsaw:{k:"anime",t:"Chainsaw Man",y:"2022–",by:"MAPPA",f:["1 season","Action","Horror"],imdb:"tt13616990",s:"Denji, a debt-ridden devil hunter, fuses with his chainsaw devil and joins a government squad.",d:[["crunchyroll","t"],["hulu","t"]],x:["chainsawmanga"]},
 opanime:{k:"anime",t:"One Piece",y:"1999–",by:"Toei Animation",f:["Ongoing","Adventure"],imdb:"tt0388629",s:"Monkey D. Luffy, a pirate with a body of rubber, sails the Grand Line in search of a legendary treasure.",d:[["crunchyroll","t"],["netflix","s"]],x:["opmanga","oplive"]},
 // search-only titles (state demonstrations)
 perfectdays:{k:"film",t:"Perfect Days",y:"2023",by:"Wim Wenders",f:["2h 4m","Drama"],imdb:"tt27503384",s:"A Tokyo toilet cleaner lives by quiet routines of music, books and trees, until visitors from his past arrive.",d:[]},
 aftersun:{k:"film",t:"Aftersun",y:"2022",by:"Charlotte Wells",f:["1h 42m","Drama"],imdb:"tt19770238",s:"A woman revisits the holiday in Turkey she took with her young father twenty years earlier.",d:[["mubi","t"]]},
 // albums (no singles, ever)
 showgirl:{k:"album",t:"The Life of a Showgirl",y:"2025",by:"Taylor Swift",a:"taylor",f:["12 tracks","Pop"],s:"Swift's twelfth studio album, written during the Eras Tour with Max Martin and Shellback.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 dtmf:{k:"album",t:"DeBÍ TiRAR MáS FOToS",y:"2025",by:"Bad Bunny",a:"badbunny",f:["17 tracks","Latin","Reggaeton"],s:"A love letter to Puerto Rico that folds plena, salsa and bomba into reggaeton.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 mbf:{k:"album",t:"Man's Best Friend",y:"2025",by:"Sabrina Carpenter",a:"sabrina",f:["12 tracks","Pop"],s:"The follow-up to Short n' Sweet, made again with Jack Antonoff and John Ryan.",d:[["spotify","t"],["ytmusic","s"],["applemusic","t"]]},
 gnx:{k:"album",t:"GNX",y:"2024",by:"Kendrick Lamar",a:"kendrick",f:["12 tracks","Hip-hop"],s:"A surprise-released West Coast record made largely with Jack Antonoff and Sounwave.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 sns:{k:"album",t:"Short n' Sweet",y:"2024",by:"Sabrina Carpenter",a:"sabrina",f:["12 tracks","Pop"],s:"Carpenter's sixth album, a set of wry, lovesick pop songs.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 hmhas:{k:"album",t:"Hit Me Hard and Soft",y:"2024",by:"Billie Eilish",a:"billie",f:["10 tracks","Alternative pop"],s:"Billie Eilish and Finneas's third album, built to be heard front to back.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 chromakopia:{k:"album",t:"Chromakopia",y:"2024",by:"Tyler, The Creator",a:"tyler",f:["14 tracks","Hip-hop"],s:"Tyler's eighth album, a masked, confessional record about turning thirty-three.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 sos:{k:"album",t:"SOS",y:"2022",by:"SZA",a:"sza",f:["23 tracks","R&B"],s:"SZA's long-awaited second album, moving between R&B, pop-punk and rap.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 brat:{k:"album",t:"BRAT",y:"2024",by:"Charli xcx",f:["15 tracks","Dance pop"],s:"A club record about fame, rivalry and doubt.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 // artists
 taylor:{k:"artist",t:"Taylor Swift",y:"Since 2006",by:"Pennsylvania, USA",f:["Pop","Country","Singer-songwriter"],s:"A singer-songwriter whose albums moved from country to pop to indie folk and back to pop.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 badbunny:{k:"artist",t:"Bad Bunny",y:"Since 2016",by:"Vega Baja, Puerto Rico",f:["Latin trap","Reggaeton"],s:"The Puerto Rican rapper and singer who made Spanish-language records the most streamed in the world.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 sabrina:{k:"artist",t:"Sabrina Carpenter",y:"Since 2014",by:"Pennsylvania, USA",f:["Pop"],s:"A singer and actor whose wry, retro-leaning pop broke through in 2024.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 kendrick:{k:"artist",t:"Kendrick Lamar",y:"Since 2003",by:"Compton, California",f:["Hip-hop"],s:"A Pulitzer-winning rapper whose albums read as linked chapters about Compton, fame and faith.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 billie:{k:"artist",t:"Billie Eilish",y:"Since 2015",by:"Los Angeles, California",f:["Alternative pop"],s:"A singer who records with her brother Finneas, known for hushed, close-miked songs.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 sza:{k:"artist",t:"SZA",y:"Since 2012",by:"New Jersey, USA",f:["R&B"],s:"An R&B singer-songwriter known for diaristic, genre-drifting records.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 weeknd:{k:"artist",t:"The Weeknd",y:"Since 2010",by:"Toronto, Canada",f:["R&B","Pop"],s:"A Toronto singer who moved from anonymous mixtapes to stadium synth-pop.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 tyler:{k:"artist",t:"Tyler, The Creator",y:"Since 2007",by:"Los Angeles, California",f:["Hip-hop"],s:"A rapper and producer who builds each album around a character and a look.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 rosalia:{k:"artist",t:"Rosalía",y:"Since 2017",by:"Sant Esteve Sesrovires, Spain",f:["Flamenco pop","Experimental"],s:"A Catalan singer who reworked flamenco into experimental pop.",d:[["spotify","t"],["ytmusic","t"],["applemusic","t"]]},
 // books
 phm:{k:"book",t:"Project Hail Mary",y:"2021",by:"Andy Weir",f:["Novel","Science fiction"],isbn:"9780593135204",s:"A science teacher wakes alone on a spacecraft with no memory and one job: save Earth.",d:[["kindle","t"],["playbooks","t"],["applebooks","t"]]},
 wok:{k:"book",t:"The Way of Kings",y:"2010",by:"Brandon Sanderson",f:["Novel","Epic fantasy","The Stormlight Archive 1"],isbn:"9780765326355",s:"On a storm-swept world, a soldier, a scholar and a highprince are drawn into a war older than memory.",d:[["kindle","t"],["playbooks","t"],["kobo","t"]]},
 fourthwing:{k:"book",t:"Fourth Wing",y:"2023",by:"Rebecca Yarros",f:["Novel","Romantasy"],isbn:"9781649374042",s:"A scribe-in-training is forced into a war college where cadets bond with dragons or die trying.",d:[["kindle","t"],["playbooks","t"]]},
 james:{k:"book",t:"James",y:"2024",by:"Percival Everett",f:["Novel","Literary fiction"],isbn:"9780385550369",s:"Huckleberry Finn's river journey, retold by the enslaved man who travels with him.",d:[["kindle","t"],["playbooks","t"],["kobo","t"]]},
 intermezzo:{k:"book",t:"Intermezzo",y:"2024",by:"Sally Rooney",f:["Novel","Literary fiction"],isbn:"9780374602635",s:"Two grieving brothers in Dublin circle each other and the women they love after their father's death.",d:[["kindle","t"],["playbooks","t"]]},
 dunebook:{k:"book",t:"Dune",y:"1965",by:"Frank Herbert",f:["Novel","Science fiction"],isbn:"9780441172719",s:"On the desert planet Arrakis, a noble heir becomes the prophet of the people who guard its spice.",d:[["kindle","t"],["playbooks","t"],["kobo","t"],["applebooks","t"]],x:["dune2"]},
 tomorrow:{k:"book",t:"Tomorrow, and Tomorrow, and Tomorrow",y:"2022",by:"Gabrielle Zevin",f:["Novel","Literary fiction"],isbn:"9780593321201",s:"Two friends make video games together across thirty years of love, rivalry and loss.",d:[["kindle","t"],["playbooks","t"]]},
 piranesi:{k:"book",t:"Piranesi",y:"2020",by:"Susanna Clarke",f:["Novel","Fantasy"],isbn:"9781635575637",s:"A man lives alone in an endless house of statues and tides, recording everything he finds.",d:[["kindle","t"],["playbooks","t"]]},
 // manga
 opmanga:{k:"manga",t:"One Piece",y:"1997–",by:"Eiichiro Oda",f:["Manga","Ongoing"],s:"Luffy sets out to find the One Piece and become King of the Pirates.",d:[["mangaplus","t"],["viz","t"]],x:["opanime","oplive"]},
 kagurabachi:{k:"manga",t:"Kagurabachi",y:"2023–",by:"Takeru Hokazono",f:["Manga","Ongoing"],s:"A swordsmith's son hunts the sorcerers who murdered his father and stole his enchanted blades.",d:[["mangaplus","t"],["viz","t"]]},
 dandadan:{k:"manga",t:"Dandadan",y:"2021–",by:"Yukinobu Tatsu",f:["Manga","Ongoing"],s:"A girl who believes in ghosts and a boy who believes in aliens both turn out to be right.",d:[["mangaplus","t"],["viz","t"]]},
 sakamoto:{k:"manga",t:"Sakamoto Days",y:"2020–",by:"Yuto Suzuki",f:["Manga","Ongoing"],s:"The greatest hitman alive retires to run a convenience store, and his old world keeps walking in.",d:[["mangaplus","t"],["viz","t"]]},
 chainsawmanga:{k:"manga",t:"Chainsaw Man",y:"2018–",by:"Tatsuki Fujimoto",f:["Manga","Ongoing"],s:"Denji hunts devils for Public Safety with a chainsaw where his heart used to be.",d:[["mangaplus","t"],["viz","t"]],x:["chainsaw"]},
 jjkmanga:{k:"manga",t:"Jujutsu Kaisen",y:"2018–2024",by:"Gege Akutami",f:["Manga","Complete"],s:"Yuji Itadori is drawn into the hidden war between sorcerers and curses.",d:[["mangaplus","t"],["viz","t"]],x:["jjk"]},
 spyfamily:{k:"manga",t:"Spy x Family",y:"2019–",by:"Tatsuya Endo",f:["Manga","Ongoing"],s:"A spy, an assassin and a telepath pretend to be a family, each hiding the truth from the others.",d:[["mangaplus","t"],["viz","t"]]},
 bluebox:{k:"manga",t:"Blue Box",y:"2021–",by:"Kouji Miura",f:["Manga","Ongoing"],s:"A badminton player falls for the basketball star who ends up living in his family's house.",d:[["mangaplus","t"],["viz","t"]]},
 frierenmanga:{k:"manga",t:"Frieren: Beyond Journey's End",y:"2020–",by:"Kanehito Yamada, Tsukasa Abe",f:["Manga","Ongoing"],s:"The manga that follows Frieren's long walk after the hero's party has passed.",d:[["kindle","t"],["playbooks","t"]],x:["frieren"]},
 // comics
 lore:{k:"comic",t:"Lore Olympus",y:"2018–2024",by:"Rachel Smythe",f:["Webcomic","Romance","Myth"],s:"A modern retelling of Persephone and Hades.",d:[["webtoon","t"]]},
 towerofgod:{k:"comic",t:"Tower of God",y:"2010–",by:"SIU",f:["Webcomic","Fantasy"],s:"A boy follows the girl he loves into a tower whose every floor holds a new test.",d:[["webtoon","t"]]},
 truebeauty:{k:"comic",t:"True Beauty",y:"2018–2024",by:"Yaongyi",f:["Webcomic","Romance"],s:"A high schooler hides behind her makeup skills until a classmate learns her secret.",d:[["webtoon","t"]]},
 unordinary:{k:"comic",t:"unOrdinary",y:"2016–",by:"uru-chan",f:["Webcomic","Superpower"],s:"In a school ranked by superpowers, a boy with none keeps his past hidden.",d:[["webtoon","t"]]},
 absbatman:{k:"comic",t:"Absolute Batman",y:"2024–",by:"Scott Snyder, Nick Dragotta",f:["Comic series","DC"],s:"A Batman without the Wayne fortune, built from scratch in a harder Gotham.",d:[["dcui","t"],["kindle","t"]]},
 watchmen:{k:"comic",t:"Watchmen",y:"1986–1987",by:"Alan Moore, Dave Gibbons",f:["Limited series","DC"],s:"Retired costumed vigilantes investigate the murder of one of their own in an alternate 1985.",d:[["dcui","t"],["kindle","t"]]},
 longhalloween:{k:"comic",t:"Batman: The Long Halloween",y:"1996–1997",by:"Jeph Loeb, Tim Sale",f:["Limited series","DC"],s:"A killer strikes on every holiday for a year, and a young Batman races to find out who.",d:[["dcui","t"],["kindle","t"]]},
 allstar:{k:"comic",t:"All-Star Superman",y:"2005–2008",by:"Grant Morrison, Frank Quitely",f:["Limited series","DC"],s:"Dying from an overdose of sunlight, Superman sets out to finish twelve last labours.",d:[["dcui","t"],["kindle","t"]]},
};

var VERB = {film:"watch",series:"watch",anime:"watch",album:"listen",artist:"listen",book:"read",manga:"read",comic:"read"};

var KIND = {film:"Film",series:"Series",anime:"Anime series",album:"Album",artist:"Artist",book:"Book",manga:"Manga",comic:"Comic"};

var SHAPE = {album:"sq",artist:"ci",book:"bk",manga:"bk",comic:"bk"};

var SHELVES = [
 {id:"netflix",v:"watch",title:"Popular on Netflix",src:"Streaming Catalogs addon",apps:["netflix"],feed:"catalog",items:["stranger","wednesday","squid","arcane","oplive","glassonion","roma","vinland"]},
 {id:"albums",v:"listen",title:"Top albums this week",apps:["spotify","ytmusic"],feed:"chart",chips:{spotify:["showgirl","dtmf","mbf","gnx","sns","hmhas","chromakopia","sos","brat"],ytmusic:["dtmf","showgirl","gnx","sos","mbf","chromakopia","hmhas","sns","brat"]},rank:true},
 {id:"prime",v:"watch",title:"Popular on Prime Video",src:"Streaming Catalogs addon",apps:["prime"],feed:"catalog",items:["theboys","fallout","reacher","invincible","fleabag","pastlives","eeaao"]},
 {id:"books",v:"read",title:"Bestselling books",src:"Kindle store",apps:["kindle"],feed:"store",items:["phm","wok","fourthwing","james","intermezzo","dunebook","tomorrow","piranesi"],rank:true},
 {id:"hbomax",v:"watch",title:"Popular on HBO Max",src:"Streaming Catalogs addon",apps:["hbomax"],feed:"catalog",items:["dune2","lastofus","whitelotus","hotd","penguin","succession","spirited"]},
 {id:"mangaplus",v:"read",title:"MANGA Plus ranking",src:"MANGA Plus",apps:["mangaplus"],feed:"store",items:["opmanga","kagurabachi","dandadan","sakamoto","chainsawmanga","jjkmanga","spyfamily","bluebox"],rank:true},
 {id:"artists",v:"listen",title:"Top artists this week",apps:["spotify","ytmusic"],feed:"chart",chips:{spotify:["taylor","badbunny","sabrina","kendrick","billie","sza","weeknd","tyler","rosalia"],ytmusic:["badbunny","taylor","kendrick","sabrina","weeknd","sza","billie","rosalia","tyler"]},rank:true},
 {id:"disney",v:"watch",title:"Popular on Disney+",src:"Streaming Catalogs addon",apps:["disney"],feed:"catalog",items:["andor","shogun","thebear","loki","mando","insideout2"]},
 {id:"crunchyroll",v:"watch",title:"Popular on Crunchyroll",src:"Streaming Catalogs addon",apps:["crunchyroll"],feed:"catalog",items:["frieren","jjk","chainsaw","opanime","vinland"]},
 {id:"appletv",v:"watch",title:"Popular on Apple TV",src:"Streaming Catalogs addon",apps:["appletv"],feed:"catalog",items:["severance","slowhorses","tedlasso","silo","pachinko","foundation"]},
 {id:"webtoon",v:"read",title:"Popular on WEBTOON",src:"WEBTOON",apps:["webtoon"],feed:"store",items:["lore","towerofgod","truebeauty","unordinary"]},
 {id:"dcui",v:"read",title:"Popular on DC Universe Infinite",src:"DC Universe Infinite",apps:["dcui"],feed:"store",items:["absbatman","watchmen","longhalloween","allstar"]},
];

var TRY = ["Dune","One Piece","Frieren","Kendrick Lamar","Brandon Sanderson","Perfect Days"];

Object.keys(T).forEach(function (id) { T[id].id = id; });
