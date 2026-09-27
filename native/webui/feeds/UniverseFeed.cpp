#include "ActionRegistry.h"
#include "ContinueFeed.h"
#include "FeedHttp.h"
#include "FeedRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"
#include "../../engine/ExtensionsStore.h"
#include "../../engine/MalCatalog.h"

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <utility>

namespace {
constexpr auto kOnePiece = "com.colosseum.universe.onepiece";
constexpr auto kCosmere = "com.colosseum.universe.cosmere";
constexpr auto kDcau = "com.colosseum.universe.dcau";
constexpr auto kStarWars = "com.colosseum.universe.starwars";

// Universes.js:43-849 is the old page's curated, provider-agnostic canon. W2-2 moves
// that immutable data into this native feed so the web page does not read live QML.
static const QByteArray kCurationJson =
R"J0([{"name":"Cosmere","c1":"#101927","category":"cosmere","blurb":"Brandon Winn Sanderson is an American author of high fantasy, science fiction, and young adult books. His best known novels include the Mistborn series and The Stormlight Archive, which are set in the \"Cosmere\", a fictional universe.","blurbSource":"Wikipedia — Brandon Sanderson","banner":"https://upload.wikimedia.org/wikipedia/commons/thumb/9/99/Window_to_the_Cosmos.jpg/1920px-Window_to_the_Cosmos.jpg","chips":[{"t":"6 Systems","ic":"books"},{"t":"26 Books & Stories","ic":"books"},{"t":"One Connected Epic","ic":"books"}],"cosmereStarters":[{"label":"The balanced beginning","short":"MISTBORN","note":"A complete fantasy heist with the clearest doorway into Sanderson's magic.","query":"Mistborn The Final Empire Brandon Sanderson"},{"label":"The deep end","short":"STORMLIGHT","note":"A vast epic for readers ready to begin with the Cosmere at full scale.","query":"The Way of Kings Brandon Sanderson"},{"label":"The standalone voyage","short":"TRESS","note":"A warm, self-contained adventure with the wider universe glinting beneath it.","query":"Tress of the Emerald Sea Brandon Sanderson"}],"cosmereWorlds":[{"name":"Scadrial","epithet":"METAL · ASH · REBELLION","accent":"#b8734a","books":[{"label":"Mistborn — Era One","query":"Mistborn The Final Empire Brandon Sanderson"},{"label":"Mistborn — Era Two","query":"The Alloy of Law Brandon Sanderson"}]},{"name":"Roshar","epithet":"STORMS · OATHS · RADIANCE","accent":"#78cfe3","books":[{"label":"The Stormlight Archive","query":"The Way of Kings Brandon Sanderson"},{"label":"Edgedancer","query":"Edgedancer Brandon Sanderson"},{"label":"Dawnshard","query":"Dawnshard Brandon Sanderson"}]},{"name":"Sel","epithet":"GLYPHS · SOULS · DEVOTION","accent":"#d9e8ff","books":[{"label":"Elantris","query":"Elantris Brandon Sanderson"},{"label":"The Emperor's Soul","query":"The Emperor's Soul Brandon Sanderson"}]},{"name":"Nalthis","epithet":"BREATH · COLOR · AWAKENING","accent":"#d688b4","books":[{"label":"Warbreaker","query":"Warbreaker Brandon Sanderson"}]},{"name":"Taldain","epithet":"SAND · SUN · MASTERY","accent":"#e5c77c","books":[{"label":"White Sand","query":"White Sand Brandon Sanderson"}]},{"name":"Farther Worlds","epithet":"OCEANS · DREAMS · STARLIGHT","accent":"#8ec7b5","books":[{"label":"Tress of the Emerald Sea","query":"Tress of the Emerald Sea Brandon Sanderson"},{"label":"Yumi and the Nightmare Painter","query":"Yumi and the Nightmare Painter Brandon Sanderson"},{"label":"The Sunlit Man","query":"The Sunlit Man Brandon Sanderson"},{"label":"Isles of the Emberdark","query":"Isles of the Emberdark Brandon Sanderson"},{"label":"Arcanum Unbounded","query":"Arcanum Unbounded Brandon Sanderson"}]}],"cosmereSeries":[{"name":"Mistborn — Era One","epithet":"THE ORIGINAL TRILOGY","accent":"#b8734a","books":[{"label":"01","query":"Mistborn The Final Empire Brandon Sanderson"},{"label":"02","query":"The Well of Ascension Brandon Sanderson"},{"label":"03)J0"
R"J1(","query":"The Hero of Ages Brandon Sanderson"}]},{"name":"Mistborn — Era Two","epithet":"WAX & WAYNE","accent":"#ce9567","books":[{"label":"01","query":"The Alloy of Law Brandon Sanderson"},{"label":"02","query":"Shadows of Self Brandon Sanderson"},{"label":"03","query":"The Bands of Mourning Brandon Sanderson"},{"label":"04","query":"The Lost Metal Brandon Sanderson"}]},{"name":"The Stormlight Archive","epithet":"FIRST ARC + NOVELLAS","accent":"#78cfe3","books":[{"label":"01","query":"The Way of Kings Brandon Sanderson"},{"label":"02","query":"Words of Radiance Brandon Sanderson"},{"label":"2.5","query":"Edgedancer Brandon Sanderson"},{"label":"03","query":"Oathbringer Brandon Sanderson"},{"label":"3.5","query":"Dawnshard Brandon Sanderson"},{"label":"04","query":"Rhythm of War Brandon Sanderson"},{"label":"05","query":"Wind and Truth Brandon Sanderson"}]},{"name":"Selish Stories","epithet":"ELANTRIS + THE EMPEROR'S SOUL","accent":"#d9e8ff","books":[{"label":"ELANTRIS","query":"Elantris Brandon Sanderson"},{"label":"SHARDWORLD NOVELLA","query":"The Emperor's Soul Brandon Sanderson"}]},{"name":"Hoid's Travails","epithet":"STORIES TOLD ACROSS THE STARS","accent":"#8ec7b5","books":[{"label":"01","query":"Tress of the Emerald Sea Brandon Sanderson"},{"label":"02","query":"Yumi and the Nightmare Painter Brandon Sanderson"},{"label":"03","query":"The Fires of December Brandon Sanderson"}]},{"name":"Cosmere Standalones","epithet":"COMPLETE WORLDS IN ONE VOLUME","accent":"#d688b4","books":[{"label":"NALTHIS","query":"Warbreaker Brandon Sanderson"},{"label":"CANTICLE","query":"The Sunlit Man Brandon Sanderson"},{"label":"FIRST OF THE SUN","query":"Isles of the Emberdark Brandon Sanderson"},{"label":"THRENODY","query":"Shadows for Silence in the Forests of Hell Brandon Sanderson"}]},{"name":"White Sand","epithet":"THE TALDAIN GRAPHIC NOVEL","accent":"#e5c77c","books":[{"label":"COMPLETE EDITION","query":"White Sand Omnibus Brandon Sanderson"}]},{"name":"Collections & Secret Histories","epithet":"THE CONNECTIONS BENEATH","accent":"#9aa8c6","books":[{"label":"THE COSMERE COLLECTION","query":"Arcanum Unbounded Brandon Sanderson"},{"label":"MISTBORN NOVELLA","query":"Mistborn Secret History Brandon Sanderson"}]}]},{"name":"Marvel Cinematic Universe","c1":"#1a2436","category":"cinematic","blurb":"The Marvel Cinematic Universe (MCU) is an American media franchise and shared universe centered on a series of superhero films produced by Marvel Studios. The films are based on characters from American comic books published by Marvel Comics.","blurbSource":"Wikipedia — Marvel Cinematic Universe","banner":"https://image.tmdb.org/t/p/w1280/gHLs7Fy3DzLmLsD4lmfqL55KGcl.jpg","continueLabel":"Continue — Loki S2","shows":[{"t":"WandaVision","id":"tt9140560"},{"t":"The Falcon and the Winter Soldier","id":"tt9208876"},{"t":"Loki","id":"tt9140554"},{"t":"What If...?","id":"tt10168312"},{"t":"Hawkeye","id":"tt10160804"},{"t":"Moon Knight","id":"tt10234724"},{"t":"Ms. Marvel",")J1"
R"J2(id":"tt10857164"},{"t":"I Am Groot","id":"tt13623148"},{"t":"She-Hulk: Attorney at Law","id":"tt10857160"},{"t":"Secret Invasion","id":"tt13157618"},{"t":"Echo","id":"tt13966962"},{"t":"X-Men '97","id":"tt16026746"},{"t":"Agatha All Along","id":"tt15571732"},{"t":"Your Friendly Neighborhood Spider-Man","id":"tt16027074"},{"t":"Daredevil: Born Again","id":"tt18923754"},{"t":"Ironheart","id":"tt13623126"},{"t":"Eyes of Wakanda","id":"tt13968252"},{"t":"Marvel Zombies","id":"tt16027014"},{"t":"Wonder Man","id":"tt21066182"},{"t":"VisionQuest","id":"tt23112594"}],"films":[{"t":"Werewolf by Night","id":"tt15318872"},{"t":"The Guardians of the Galaxy Holiday Special","id":"tt13623136"},{"t":"The Punisher: One Last Kill","id":"tt36042156"}],"mcuShowPhases":{"tt9140560":"IV","tt9208876":"IV","tt9140554":"IV","tt10168312":"IV","tt10160804":"IV","tt10234724":"IV","tt10857164":"IV","tt13623148":"IV","tt10857160":"IV","tt13157618":"V","tt13966962":"V","tt15571732":"V","tt16027074":"V","tt18923754":"V","tt13623126":"V","tt13968252":"VI","tt16027014":"VI","tt21066182":"VI","tt23112594":"VI","tt15318872":"IV","tt13623136":"IV","tt36042156":"VI"},"seriesQueries":["wandavision","the falcon and the winter soldier","loki","what if","hawkeye","moon knight","ms marvel","i am groot","she hulk attorney at law","secret invasion","echo","x-men 97","agatha all along","your friendly neighborhood spider-man","daredevil born again","ironheart","eyes of wakanda","marvel zombies","wonder man","visionquest"],"movieQueries":["werewolf by night","guardians of the galaxy holiday special","the punisher one last kill"],"chips":[{"t":"34 Films","ic":"movies"},{"t":"20 Series","ic":"movies"},{"t":"3 Specials","ic":"movies"}]},{"name":"Dragon Ball","c1":"#e8791e","category":"dragonball","blurb":"Dragon Ball is a Japanese media franchise created by Akira Toriyama. The series follows the adventures of protagonist Son Goku from his childhood through adulthood as he trains in martial arts.","blurbSource":"Wikipedia — Dragon Ball","banner":"https://s4.anilist.co/file/anilistcdn/media/manga/banner/30042-4aSSSOxCNWgE.jpg","saga":[{"star":1,"era":"Dragon Ball","t":"Dragon Ball","id":"tt0088509","year":"1986","note":"The boy, the tail, the first search"},{"star":2,"era":"Dragon Ball Z","t":"Dragon Ball Z","id":"tt0121220","year":"1989","note":"Saiyans arrive, and the sky gets higher"},{"star":3,"era":"Dragon Ball GT","t":"Dragon Ball GT","id":"tt0139774","year":"1996","note":"Off Earth, chasing the Black Star balls"},{"star":4,"era":"Dragon Ball Z Kai","t":"Dragon Ball Z Kai","id":"tt1409055","year":"2009","note":"Z re-cut, tighter, closer to the manga"},{"star":5,"era":"Dragon Ball Super","t":"Dragon Ball Super","id":"tt4644488","year":"2015","note":"Gods of destruction, other universes"},{"star":6,"era":"Super Dragon Ball Heroes","t":"Super Dragon Ball Heroes","id":"tt8433216","year":"2018","note":"Every hero, every timeline at once"},{"star":7,"era":"Dragon Ball Daima","t":"Dragon Ball Daima)J2"
R"J3(","id":"tt29485149","year":"2024","note":"Toriyama's parting gift — small again"}],"filmEras":[{"era":"The Dragon Ball Films","films":[{"t":"Curse of the Blood Rubies","id":"tt0142251","year":"1986"},{"t":"Sleeping Princess in Devil's Castle","id":"tt0142249","year":"1987"},{"t":"Mystical Adventure","id":"tt0142248","year":"1988"},{"t":"The Path to Power","id":"tt0142250","year":"1996"}]},{"era":"The Z Films & Specials","films":[{"t":"Dead Zone","id":"tt0142235","year":"1989"},{"t":"The World's Strongest","id":"tt0142240","year":"1990"},{"t":"The Tree of Might","id":"tt0142233","year":"1990"},{"t":"Bardock — The Father of Goku","id":"tt0142245","year":"1990"},{"t":"Lord Slug","id":"tt0142244","year":"1991"},{"t":"Cooler's Revenge","id":"tt1125254","year":"1991"},{"t":"The Return of Cooler","id":"tt0142237","year":"1992"},{"t":"Super Android 13!","id":"tt0142241","year":"1992"},{"t":"Broly — The Legendary Super Saiyan","id":"tt0142242","year":"1993"},{"t":"The History of Trunks","id":"tt0142247","year":"1993"},{"t":"Bojack Unbound","id":"tt0142238","year":"1993"},{"t":"Plan to Eradicate the Saiyans","id":"tt1286785","year":"1993"},{"t":"Broly — Second Coming","id":"tt0142239","year":"1994"},{"t":"Bio-Broly","id":"tt0142234","year":"1994"},{"t":"Fusion Reborn","id":"tt0142236","year":"1995"},{"t":"Wrath of the Dragon","id":"tt0142243","year":"1995"},{"t":"GT: A Hero's Legacy","id":"tt0142232","year":"1997"}]},{"era":"The Modern Films","films":[{"t":"Battle of Gods","id":"tt2263944","year":"2013"},{"t":"Resurrection 'F'","id":"tt3819668","year":"2015"},{"t":"Dragon Ball Super: Broly","id":"tt7961060","year":"2018"},{"t":"Dragon Ball Super: Super Hero","id":"tt14614892","year":"2022"}]}],"manga":[{"t":"Dragon Ball","cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30042-4SetGiEbGc9x.jpg"},{"t":"Dragon Ball Super","cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx86508-QSahE7mTFEXl.png"},{"t":"Dragon Ball SD","cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx53446-iJhUffEy8U9u.jpg"},{"t":"Reincarnated as Yamcha!","q":"Dragon Ball Yamcha","cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx98030-ljTCpp4oILtu.jpg"},{"t":"Episode of Bardock","q":"Dragon Ball Episode of Bardock","cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx56373-VBxH4drN6jJ1.png"},{"t":"Resurrection 'F'","q":"Dragon Ball Z Resurrection F","cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx94109-oCtSkyO2NOUW.jpg"},{"t":"Dragon Ball Minus","q":"Dragon Ball Minus Departure Fated Child","cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx97900-EqScEWX0U6Tj.png"},{"t":"Goku & Friends Return!!","q":"Dragon Ball Son Goku and His Friends Return","cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx46110-J5o0hRODa79o.jpg"}],"firstWatch":{"t":"Dragon Ball","id":"tt0088509"},"chips":[{"t":"8 Manga","ic":"manga"},{)J3"
R"J4("t":"7 Anime","ic":"movies"},{"t":"25 Films","ic":"movies"}]},{"name":"Weekly Shonen Jump","c1":"#3a1414","category":"magazine","blurb":"Weekly Shōnen Jump is a weekly shōnen manga anthology published in Japan by Shueisha under the Jump line of magazines. It is one of the longest-running manga magazines, with the first issue being released on July 11, 1968.","blurbSource":"Wikipedia — Weekly Shōnen Jump","banner":"https://upload.wikimedia.org/wikipedia/en/0/02/Jump-Cover-1.jpg","malMagazineId":83,"milestones":[{"year":"1968","fact":"launches July 11 at 105,000 copies"},{"year":"1995","fact":"peaks at 6.53 million copies a week"},{"year":"2026","fact":"still above a million copies weekly"}],"flagships":[{"t":"KochiKame","a":"Osamu Akimoto","al":30733,"from":1976,"to":2021,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx30733-QmyPwBhjbgyX.jpg"},{"t":"Ring ni Kakero","a":"Masami Kurumada","al":44231,"from":1977,"to":1981,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/b44231-AOphgOiTzTEL.jpg"},{"t":"Dr. Slump","a":"Akira Toriyama","al":30796,"from":1980,"to":1984,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30796-PFF4n7Y2spz9.png"},{"t":"Captain Tsubasa","a":"Yoichi Takahashi","al":31789,"from":1981,"to":1988,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx31789-qiNlHJDMJncz.png"},{"t":"Fist of the North Star","a":"Buronson & Hara","al":31149,"from":1983,"to":1988,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx31149-TOwqxOlY1Zs0.jpg"},{"t":"Dragon Ball","a":"Akira Toriyama","al":30042,"from":1984,"to":1995,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30042-4SetGiEbGc9x.jpg"},{"t":"Saint Seiya","a":"Masami Kurumada","al":31045,"from":1985,"to":1990,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx31045-YUcWBMk7RpeK.png"},{"t":"JoJo — Phantom Blood","a":"Hirohiko Araki","al":31517,"from":1986,"to":1987,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx31517-Y3pL6OTH74Iq.png"},{"t":"Slam Dunk","a":"Takehiko Inoue","al":30051,"from":1990,"to":1996,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30051-5KJyPlO7z5F4.png"},{"t":"Yu Yu Hakusho","a":"Yoshihiro Togashi","al":30053,"from":1990,"to":1994,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30053-wCR6xyGzeUYo.png"},{"t":"Rurouni Kenshin","a":"Nobuhiro Watsuki","al":30022,"from":1994,"to":1999,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30022-jxTlViun1o10.jpg"},{"t":"One Piece","a":"Eiichiro Oda","al":30013,"from":1997,"to":0,"publishing":true,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30013-BeslEMqiPhlk.jpg"},{"t":"Hunter x Hunter","a":"Yoshihiro Togashi","al":30026,"from":1998,"to":0,"publishing":true,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30026-uCv)J4"
R"J5(XMudMzmwI.jpg"},{"t":"Naruto","a":"Masashi Kishimoto","al":30011,"from":1999,"to":2014,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx30011-9yUF1dXWgDOx.jpg"},{"t":"Bleach","a":"Tite Kubo","al":30012,"from":2001,"to":2016,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30012-1epmVfTSv2rr.png"},{"t":"Eyeshield 21","a":"Inagaki & Murata","al":30043,"from":2002,"to":2009,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30043-b2SqUjwSzfIH.png"},{"t":"Death Note","a":"Ohba & Obata","al":30021,"from":2003,"to":2006,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30021-FE6kmrfpuKyb.jpg"},{"t":"Bakuman","a":"Ohba & Obata","al":39711,"from":2008,"to":2012,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx39711-tjPWXT1AW321.jpg"},{"t":"Haikyu!!","a":"Haruichi Furudate","al":65243,"from":2012,"to":2020,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx65243-mR4MnJFmfaOF.png"},{"t":"Assassination Classroom","a":"Yusei Matsui","al":69883,"from":2012,"to":2016,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx69883-zDt4DUXkQS5N.png"},{"t":"My Hero Academia","a":"Kohei Horikoshi","al":85486,"from":2014,"to":2024,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx85486-INqnYx8gL3eX.jpg"},{"t":"Demon Slayer: Kimetsu no Yaiba","a":"Koyoharu Gotouge","al":87216,"from":2016,"to":2020,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx87216-c9bSNVD10UuD.png"},{"t":"The Promised Neverland","a":"Shirai & Demizu","al":87423,"from":2016,"to":2020,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx87423-gPNtu8QbGped.jpg"},{"t":"Dr. Stone","a":"Inagaki & Boichi","al":98416,"from":2017,"to":2024,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/b98416-L44f4idEGMAX.jpg"},{"t":"Jujutsu Kaisen","a":"Gege Akutami","al":101517,"from":2018,"to":2024,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx101517-H3TdM3g5ZUe9.jpg"},{"t":"Sakamoto Days","a":"Yuto Suzuki","al":125828,"from":2020,"to":0,"publishing":true,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx125828-p78Z8SflkfmO.jpg"},{"t":"Blue Box","a":"Kouji Miura","al":132182,"from":2021,"to":2026,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx132182-maXh2QzYPrqR.jpg"},{"t":"Kagurabachi","a":"Takeru Hokazono","al":169355,"from":2023,"to":0,"publishing":true,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx169355-5pqzh1Wb4NOQ.png"}],"currentLineup":[{"t":"One Piece","a":"Eiichiro Oda","since":"July 1997","y":1997,"al":30013,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30013-BeslEMqiPhlk.jpg"},{"t":"Hunter x Hunter","a":"Yoshihiro Togashi","since":"March 1998","y":1998,"al":30026,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/c)J5"
R"J6(over/medium/bx30026-uCvXMudMzmwI.jpg"},{"t":"Burn the Witch","a":"Tite Kubo","since":"August 2020","y":2020,"al":116827,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx116827-dborNlGJ9K8G.png"},{"t":"Me & Roboco","a":"Shuhei Miyazaki","since":"July 2020","y":2020,"al":119499,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx119499-bO0ef8QxQozs.png"},{"t":"Sakamoto Days","a":"Yuto Suzuki","since":"November 2020","y":2020,"al":125828,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx125828-p78Z8SflkfmO.jpg"},{"t":"Witch Watch","a":"Kenta Shinohara","since":"February 2021","y":2021,"al":128896,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx128896-VJfCBLFkm4Lb.jpg"},{"t":"Akane-banashi","a":"Suenaga & Moue","since":"February 2022","y":2022,"al":144866,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx144866-ummLIg6x419I.jpg"},{"t":"RuriDragon","a":"Masaoki Shindo","since":"June 2022","y":2022,"al":150440,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx150440-QdBFoMh4YHsK.jpg"},{"t":"Nue's Exorcist","a":"Kota Kawae","since":"May 2023","y":2023,"al":163497,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx163497-HNrC3KDTVxW5.jpg"},{"t":"Kagurabachi","a":"Takeru Hokazono","since":"September 2023","y":2023,"al":169355,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx169355-5pqzh1Wb4NOQ.png"},{"t":"Ultimate Exorcist Kiyoshi","a":"Shoichi Usui","since":"June 2024","y":2024,"al":178509,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx178509-Iyc0m68gQv8U.png"},{"t":"Ichi the Witch","a":"Nishi & Usazaki","since":"September 2024","y":2024,"al":180752,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx180752-13CVO310XiEs.jpg"},{"t":"Shinobi Undercover","a":"Takegushi & Mitarashi","since":"September 2024","y":2024,"al":180881,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx180881-2NqZaTxr69jc.jpg"},{"t":"Someone Hertz","a":"Ei Yamano","since":"September 2025","y":2025,"al":198817,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx198817-6WrNkObMtjkN.jpg"},{"t":"Under Doctor","a":"Kyo Tanimoto","since":"January 2026","y":2026,"al":206835,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx206835-PWOgOewxlpUi.jpg"},{"t":"Kinato's Magic","a":"Kento Amemiya","since":"February 2026","y":2026,"al":207142,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx207142-T7DIOPtJLk50.jpg"},{"t":"Class 2-B Hero Destroyerz","a":"Hideaki Sorachi","since":"April 2026","y":2026,"al":210041,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx210041-KBPe9Ez3iBHI.jpg"},{"t":"Roku's House of Oddities","a":"Atsushi Nakamura","since":"April 2026","y":2026,"al":210422,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/)J6"
R"J7(medium/bx210422-k89sP63t4eYv.png"},{"t":"Drawn to the Fire","a":"Masayoshi Satosho","since":"April 2026","y":2026,"al":210838,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx210838-q2ytgCW6cYQB.png"},{"t":"Animal Signal","a":"Haruhara & Tsutsui","since":"June 2026","y":2026,"al":213019,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx213019-Bw406cjOq5GP.jpg"},{"t":"Hal Formula","a":"Kento Terasaka","since":"June 2026","y":2026,"al":213229,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx213229-09asKdNf3Z3e.png"},{"t":"Cannon Master","a":"Reiya Machida","since":"June 2026","y":2026,"al":213403,"cover":"https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx213403-fNxRdRYoPtEc.jpg"}],"readQueries":["One Piece","Naruto","Bleach","Dragon Ball","Hunter x Hunter","My Hero Academia","Jujutsu Kaisen","Demon Slayer: Kimetsu no Yaiba","Chainsaw Man","Death Note"],"chips":[{"t":"50+ Manga","ic":"manga"},{"t":"Weekly","ic":"manga"},{"t":"Since 1968","ic":"manga"}]},{"name":"Harry Potter","c1":"#221c30","category":"saga","archived":true,"blurb":"The Wizarding World — Rowling's seven novels, the eight films, and the Fantastic Beasts era beyond.","banner":"https://live.metahub.space/background/medium/tt1201607/img","novels":["Harry Potter and the Sorcerer's Stone","Harry Potter and the Chamber of Secrets","Harry Potter and the Prisoner of Azkaban","Harry Potter and the Goblet of Fire","Harry Potter and the Order of the Phoenix","Harry Potter and the Half-Blood Prince","Harry Potter and the Deathly Hallows","Fantastic Beasts and Where to Find Them Newt Scamander","Quidditch Through the Ages","The Tales of Beedle the Bard"],"films":["Harry Potter and the Sorcerer's Stone","Harry Potter and the Chamber of Secrets","Harry Potter and the Prisoner of Azkaban","Harry Potter and the Goblet of Fire","Harry Potter and the Order of the Phoenix","Harry Potter and the Half-Blood Prince","Harry Potter and the Deathly Hallows: Part 1","Harry Potter and the Deathly Hallows: Part 2","Fantastic Beasts and Where to Find Them","Fantastic Beasts: The Crimes of Grindelwald","Fantastic Beasts: The Secrets of Dumbledore"],"shows":[{"t":"Harry Potter","id":"tt13918446"}],"movieQueries":["Harry Potter","Fantastic Beasts"],"chips":[{"t":"10 Books","ic":"books"},{"t":"11 Films","ic":"movies"},{"t":"1 Show","ic":"movies"}]},{"name":"Lord of the Rings","c1":"#1c2414","category":"saga","archived":true,"blurb":"Tolkien's Middle-earth — the novels, Jackson's films, and the age of Rings of Power.","banner":"https://live.metahub.space/background/medium/tt0167260/img","novels":["The Hobbit","The Fellowship of the Ring","The Two Towers","The Return of the King","The Silmarillion"],"films":["The Lord of the Rings: The Fellowship of the Ring","The Lord of the Rings: The Two Towers","The Lord of the Rings: The Return of the King","The Hobbit: An Unexpected Journey","The Hobbit: The Desolation of Smaug","The Hobbit: )J7"
R"J8(The Battle of the Five Armies","The Lord of the Rings: The War of the Rohirrim",{"t":"The Lord of the Rings","id":"tt0077869"},{"t":"The Hunt for Gollum","id":"tt32328070"}],"shows":["The Lord of the Rings: The Rings of Power"],"movieQueries":["The Lord of the Rings","The Hobbit"],"seriesQueries":["The Lord of the Rings"],"readQueries":["The Hobbit Tolkien"],"chips":[{"t":"5 Novels","ic":"books"},{"t":"9 Films","ic":"movies"},{"t":"1 Show","ic":"movies"}]},{"name":"A Song of Ice and Fire","c1":"#1f2429","category":"saga","archived":true,"blurb":"Martin's Westeros — the saga still being written, and the shows that carved it into legend.","banner":"https://live.metahub.space/background/medium/tt0944947/img","novels":["A Game of Thrones","A Clash of Kings","A Storm of Swords","A Feast for Crows","A Dance with Dragons","A Knight of the Seven Kingdoms","Fire & Blood","The World of Ice & Fire","The Rise of the Dragon: An Illustrated History"],"films":[],"shows":["Game of Thrones","House of the Dragon",{"t":"A Knight of the Seven Kingdoms","id":"tt27497448"}],"seriesQueries":["Game of Thrones","House of the Dragon","a knight of the seven kingdoms"],"chips":[{"t":"9 Books","ic":"books"},{"t":"3 Shows","ic":"movies"},{"t":"Graphic Novels","ic":"comics"}]},{"name":"Naruto","c1":"#2a3212","category":"anime","archived":true,"blurb":"The Hidden Leaf's loudest ninja — Kishimoto's manga, the anime and Shippuden, and Boruto's generation.","banner":"https://s4.anilist.co/file/anilistcdn/media/manga/banner/30011-pkX1O0EFqvV7.jpg","readQueries":["Naruto","Boruto","Boruto: Two Blue Vortex","Naruto: The Seventh Hokage and the Scarlet Spring"],"seriesQueries":["naruto","boruto"],"chips":[{"t":"5 Manga","ic":"manga"},{"t":"3 Anime","ic":"movies"},{"t":"11 Films","ic":"movies"}]},{"name":"DC Animated Universe","c1":"#101622","category":"eras","archived":true,"eraKicker":"THE TIMELINE","blurb":"The Timmverse — one continuous animated world, from the noir rooftops of Gotham to the neon future of Neo-Gotham.","banner":"https://live.metahub.space/background/medium/tt0275137/img","eras":[{"era":"Gotham","kind":"series","titles":[{"t":"Batman: The Animated Series","id":"tt0103359"},{"t":"The New Batman Adventures","id":"tt0118266"}]},{"era":"Metropolis & the League","kind":"series","titles":[{"t":"Superman: The Animated Series","id":"tt0115378"},{"t":"Justice League","id":"tt0275137"},{"t":"Justice League Unlimited","id":"tt6025022"},{"t":"Static Shock","id":"tt0247729"}]},{"era":"The Future","kind":"series","titles":[{"t":"Batman Beyond","id":"tt0147746"},{"t":"The Zeta Project","id":"tt0260662"}]}],"rails":[{"title":"The Films","kind":"movie","titles":[{"t":"Batman: Mask of the Phantasm","id":"tt0106364"},{"t":"Batman & Mr. Freeze: SubZero","id":"tt0143127"},{"t":"Batman Beyond: Return of the Joker","id":"tt0233298"},{"t":"Batman: Mystery of the Batwoman","id":"tt0346578"}]}],"firstWatch":"Batman: The Animated Series","firstWatchKind":"series","firstWatchLabel":"Begin in Go)J8"
R"J9(tham","seriesQueries":["batman the animated series","the new batman adventures","superman the animated series","justice league","justice league unlimited","static shock","batman beyond","the zeta project"],"movieQueries":["batman mask of the phantasm","batman mr freeze subzero","batman beyond return of the joker","batman mystery of the batwoman"],"chips":[{"t":"8 Shows","ic":"movies"},{"t":"4 Films","ic":"movies"},{"t":"Comics","ic":"comics"}]},{"name":"Star Trek","c1":"#10141f","category":"eras","archived":true,"eraKicker":"THE FLEET","blurb":"The final frontier, charted end to end — every series and film from Kirk's five-year mission to the streaming age.","banner":"https://live.metahub.space/background/medium/tt0796366/img","eras":[{"era":"The Classic Era","kind":"series","titles":["Star Trek","Star Trek: The Animated Series","Star Trek: The Next Generation","Star Trek: Deep Space Nine","Star Trek: Voyager","Star Trek: Enterprise"]},{"era":"The Streaming Era","kind":"series","titles":["Star Trek: Discovery","Star Trek: Picard","Star Trek: Lower Decks","Star Trek: Prodigy","Star Trek: Strange New Worlds",{"t":"Star Trek: Short Treks","id":"tt9059594"},{"t":"Star Trek: Starfleet Academy","id":"tt8622160"}]},{"era":"The Original Crew","kind":"movie","titles":["Star Trek: The Motion Picture","Star Trek II: The Wrath of Khan","Star Trek III: The Search for Spock","Star Trek IV: The Voyage Home","Star Trek V: The Final Frontier","Star Trek VI: The Undiscovered Country"]},{"era":"The Next Generation","kind":"movie","titles":["Star Trek: Generations","Star Trek: First Contact","Star Trek: Insurrection","Star Trek: Nemesis"]},{"era":"The Kelvin Timeline","kind":"movie","titles":["Star Trek","Star Trek Into Darkness","Star Trek Beyond"]}],"rails":[{"title":"The Streaming Films","kind":"movie","titles":[{"t":"Star Trek: Section 31","id":"tt9603060"}]}],"comics":{"tag":"star-trek","tagId":691,"line":"The fleet in panels — IDW's voyages, Year Five to Lower Decks."},"firstWatch":"Star Trek","firstWatchKind":"series","firstWatchLabel":"Begin the five-year mission","seriesQueries":["Star Trek","Star Trek The Animated Series","star trek short treks","star trek starfleet academy"],"movieQueries":["Star Trek","Star Trek III","Star Trek V","Star Trek VI","star trek section 31"],"chips":[{"t":"13 Shows","ic":"movies"},{"t":"14 Films","ic":"movies"},{"t":"Novels","ic":"books"}]},{"name":"Star Wars","c1":"#14181c","category":"galaxy","archived":true,"blurb":"A galaxy far, far away — the nine-episode Skywalker Saga, the standalone stories, and the age of The Mandalorian.","banner":"https://live.metahub.space/background/medium/tt0080684/img","trilogies":[{"era":"The Prequels","films":["Star Wars: Episode I - The Phantom Menace","Star Wars: Episode II - Attack of the Clones","Star Wars: Episode III - Revenge of the Sith"]},{"era":"The Originals","films":["Star Wars: Episode IV - A New Hope","Star Wars: Episode V - The Empire Strikes Back","Star Wars: Episode VI - Retur)J9"
R"J10(n of the Jedi"]},{"era":"The Sequels","films":["Star Wars: Episode VII - The Force Awakens","Star Wars: Episode VIII - The Last Jedi","Star Wars: Episode IX - The Rise of Skywalker"]}],"standalones":["Rogue One: A Star Wars Story","Solo: A Star Wars Story"],"liveShows":["The Mandalorian","Andor","Obi-Wan Kenobi","The Book of Boba Fett","Ahsoka","The Acolyte","Skeleton Crew"],"animatedShows":["Star Wars: The Clone Wars","Star Wars: Rebels","Star Wars: The Bad Batch","Star Wars: Resistance","Star Wars: Visions",{"t":"Star Wars: Clone Wars","id":"tt0361243"},{"t":"Star Wars: Tales of the Jedi","id":"tt20723374"},{"t":"Star Wars: Tales of the Empire","id":"tt32019314"},{"t":"Star Wars: Tales of the Underworld","id":"tt36414431"},{"t":"Star Wars: Young Jedi Adventures","id":"tt20674124"}],"comics":{"tag":"star-wars","tagId":203,"line":"The galaxy in panels — Vader, Aphra, the High Republic and beyond."},"firstWatch":"Star Wars: Episode IV - A New Hope","movieQueries":["Star Wars","Rogue One: A Star Wars Story","Solo: A Star Wars Story"],"seriesQueries":["Star Wars","The Mandalorian","Andor","Obi-Wan Kenobi","The Book of Boba Fett","Ahsoka","The Acolyte","Skeleton Crew","clone wars","tales of the jedi","tales of the empire","tales of the underworld","young jedi adventures"],"chips":[{"t":"11 Films","ic":"movies"},{"t":"17 Shows","ic":"movies"},{"t":"Novels","ic":"books"}]},{"name":"Dune","c1":"#3a2a18","category":"saga","archived":true,"blurb":"Frank Herbert's world, end to end — the novels, the films, the graphic novel.","banner":"https://live.metahub.space/background/medium/tt15239678/img","novels":["Dune","Dune Messiah","Children of Dune","God Emperor of Dune","Heretics of Dune","Chapterhouse: Dune"],"films":[{"t":"Dune","id":"tt1160419"},"Dune: Part Two",{"t":"Dune: Part Three","id":"tt31378509"},{"t":"Dune","id":"tt0087182"}],"shows":["Dune: Prophecy",{"t":"Dune","id":"tt0142032"},{"t":"Children of Dune","id":"tt0287839"}],"movieQueries":["Dune"],"seriesQueries":["Dune","children of dune"],"comics":{"tag":"dune","tagId":11202,"line":"Arrakis in panels — the graphic adaptations and the House books."},"chips":[{"t":"6 Novels","ic":"books"},{"t":"4 Films","ic":"movies"},{"t":"3 Shows","ic":"movies"}]},{"name":"The Witcher","c1":"#26221c","category":"saga","archived":true,"blurb":"Geralt's path through a Continent of monsters — the saga, the Netflix shows, and the animated film, in one place.","banner":"https://live.metahub.space/background/medium/tt5180504/img","novels":["The Last Wish","Sword of Destiny","Blood of Elves","The Time of Contempt","Baptism of Fire","The Tower of Swallows","The Lady of the Lake","Season of Storms","Crossroads of Ravens"],"films":["The Witcher: Nightmare of the Wolf",{"t":"The Witcher: Sirens of the Deep","id":"tt15495150"},{"t":"The Rats: A Witcher Tale","id":"tt28283547"}],"shows":["The Witcher","The Witcher: Blood Origin"],"movieQueries":["witcher","witcher sirens of the deep","the rats witcher"],"seriesQueries":["witc)J10"
R"J11(her"],"comics":{"tag":"the-witcher","tagId":6975,"line":"Geralt in panels — the Dark Horse hunts."},"chips":[{"t":"9 Novels","ic":"books"},{"t":"2 Shows","ic":"movies"},{"t":"3 Films","ic":"movies"}]},{"name":"Sherlock Holmes","c1":"#1f242c","category":"saga","archived":true,"blurb":"The Baker Street canon — Doyle's nine books and the defining screen deductions, in one place.","banner":"https://live.metahub.space/background/medium/tt1475582/img","novels":["A Study in Scarlet","The Sign of the Four","The Adventures of Sherlock Holmes","The Memoirs of Sherlock Holmes","The Hound of the Baskervilles","The Return of Sherlock Holmes","The Valley of Fear","His Last Bow","The Case-Book of Sherlock Holmes"],"films":["Sherlock Holmes","Sherlock Holmes: A Game of Shadows"],"shows":["Sherlock","Elementary","The Adventures of Sherlock Holmes"],"movieQueries":["sherlock holmes"],"seriesQueries":["sherlock","elementary"],"chips":[{"t":"9 Books","ic":"books"},{"t":"2 Films","ic":"movies"},{"t":"3 Shows","ic":"movies"}]},{"name":"Jurassic Park","c1":"#1c2a20","category":"saga","archived":true,"blurb":"Crichton's islands of resurrected giants — the novels, all seven films, and the animated escapes, in one place.","banner":"https://live.metahub.space/background/medium/tt0107290/img","novels":["Jurassic Park","The Lost World Michael Crichton"],"films":["Jurassic Park","The Lost World: Jurassic Park","Jurassic Park III","Jurassic World","Jurassic World: Fallen Kingdom","Jurassic World: Dominion","Jurassic World: Rebirth"],"shows":["Jurassic World: Camp Cretaceous","Jurassic World: Chaos Theory"],"movieQueries":["jurassic"],"seriesQueries":["jurassic"],"chips":[{"t":"2 Novels","ic":"books"},{"t":"7 Films","ic":"movies"},{"t":"2 Shows","ic":"movies"}]},{"name":"Percy Jackson","c1":"#1a2430","category":"saga","archived":true,"blurb":"A demigod's quests from Camp Half-Blood to Olympus — the five books, the films, and the series, in one place.","banner":"https://live.metahub.space/background/medium/tt12324366/img","novels":["The Lightning Thief","The Sea of Monsters","The Titan's Curse","The Battle of the Labyrinth","The Last Olympian","The Chalice of the Gods","Wrath of the Triple Goddess"],"films":["Percy Jackson & the Olympians: The Lightning Thief","Percy Jackson: Sea of Monsters"],"shows":["Percy Jackson and the Olympians"],"movieQueries":["percy jackson"],"seriesQueries":["percy jackson"],"chips":[{"t":"7 Novels","ic":"books"},{"t":"2 Films","ic":"movies"},{"t":"1 Show","ic":"movies"}]},{"name":"James Bond","c1":"#1a1a1e","category":"eras","archived":true,"eraKicker":"THE DOSSIER","blurb":"Sixty years of 007 — every Eon mission from Dr. No to No Time to Die, six Bonds deep, in one place.","banner":"https://live.metahub.space/background/medium/tt0058150/img","eras":[{"era":"Sean Connery · 1962–1971","kind":"movie","titles":["Dr. No","From Russia with Love","Goldfinger","Thunderball","You Only Live Twice","Diamonds Are Forever"]},{"era":"George Lazenby · 1969","kind")J11"
R"J12(:"movie","titles":["On Her Majesty's Secret Service"]},{"era":"Roger Moore · 1973–1985","kind":"movie","titles":["Live and Let Die","The Man with the Golden Gun","The Spy Who Loved Me","Moonraker","For Your Eyes Only","Octopussy","A View to a Kill"]},{"era":"Timothy Dalton · 1987–1989","kind":"movie","titles":["The Living Daylights","Licence to Kill"]},{"era":"Pierce Brosnan · 1995–2002","kind":"movie","titles":["GoldenEye","Tomorrow Never Dies","The World Is Not Enough","Die Another Day"]},{"era":"Daniel Craig · 2006–2021","kind":"movie","titles":["Casino Royale","Quantum of Solace","Skyfall","Spectre","No Time to Die"]}],"novels":["Casino Royale","Live and Let Die","Moonraker","Diamonds Are Forever","From Russia with Love","Dr. No","Goldfinger","For Your Eyes Only","Thunderball","The Spy Who Loved Me","On Her Majesty's Secret Service","You Only Live Twice","The Man with the Golden Gun","Octopussy and The Living Daylights"],"novelsTitle":"The Fleming Shelf","comics":{"tag":"james-bond","tagId":2111,"line":"007 in panels — Dynamite's missions and the classic strips."},"firstWatch":"Dr. No","firstWatchLabel":"Begin with Dr. No","movieQueries":["Dr. No","From Russia with Love","Goldfinger","Thunderball","You Only Live Twice","Diamonds Are Forever","On Her Majesty's Secret Service","Live and Let Die","The Man with the Golden Gun","The Spy Who Loved Me","Moonraker","For Your Eyes Only","Octopussy","A View to a Kill","The Living Daylights","Licence to Kill","GoldenEye","Tomorrow Never Dies","The World Is Not Enough","Die Another Day","Casino Royale","Quantum of Solace","Skyfall","Spectre","No Time to Die"],"chips":[{"t":"25 Films","ic":"movies"},{"t":"6 Eras","ic":"movies"},{"t":"14 Books","ic":"books"}]},{"name":"Studio Ghibli","c1":"#2a3328","category":"studio","archived":true,"blurb":"Hand-drawn worlds from Miyazaki, Takahata, and kin — every Ghibli feature, Nausicaä to The Boy and the Heron.","banner":"https://live.metahub.space/background/medium/tt0245429/img","filmography":["Nausicaä of the Valley of the Wind",{"t":"Castle in the Sky","id":"tt0092067"},{"t":"Grave of the Fireflies","id":"tt0095327"},"My Neighbor Totoro","Kiki's Delivery Service",{"t":"Only Yesterday","id":"tt0102587"},"Porco Rosso",{"t":"Ocean Waves","id":"tt0108432"},"Pom Poko",{"t":"Whisper of the Heart","id":"tt0113824"},"Princess Mononoke","My Neighbors the Yamadas","Spirited Away","The Cat Returns","Howl's Moving Castle","Tales from Earthsea","Ponyo","The Secret World of Arrietty","From Up on Poppy Hill","The Wind Rises","The Tale of The Princess Kaguya","When Marnie Was There","The Boy and the Heron"],"firstWatch":"Spirited Away","firstWatchLabel":"Begin with Spirited Away","movieQueries":["Nausicaa of the Valley of the Wind","Castle in the Sky","Grave of the Fireflies","My Neighbor Totoro","Kiki's Delivery Service","Only Yesterday","Porco Rosso","Ocean Waves","Pom Poko","Whisper of the Heart","Princess Mononoke","My Neighbors the Yamadas","Spirited Away","The Cat Returns")J12"
R"J13(,"Howl's Moving Castle","Tales from Earthsea","Ponyo","The Secret World of Arrietty","From Up on Poppy Hill","The Wind Rises","The Tale of the Princess Kaguya","When Marnie Was There","The Boy and the Heron"],"chips":[{"t":"23 Films","ic":"movies"},{"t":"1984–2023","ic":"movies"}]},{"name":"Avatar: The Last Airbender","c1":"#16262c","category":"eras","archived":true,"eraKicker":"THE CANON","blurb":"One boy, four nations, a hundred-year war — the animated canon, Korra's age, and the live-action retelling.","banner":"https://live.metahub.space/background/medium/tt0417299/img","eras":[{"era":"The Animated Canon","kind":"series","titles":[{"t":"Avatar: The Last Airbender","id":"tt0417299"},{"t":"The Legend of Korra","id":"tt1695360"}]},{"era":"The Live Action","kind":"series","titles":[{"t":"Avatar: The Last Airbender","id":"tt9018736"}]}],"rails":[{"title":"The Films","kind":"movie","titles":[{"t":"The Last Airbender","id":"tt0938283"},{"t":"The Legend of Aang: The Last Airbender","id":"tt18259538"}]}],"comics":{"tag":"avatar-the-last-airbender","tagId":448,"line":"The story continues past the finale — the Dark Horse library."},"novels":["The Rise of Kyoshi","The Shadow of Kyoshi","The Dawn of Yangchen","The Legacy of Yangchen","The Reckoning of Roku","The Awakening of Roku"],"novelsTitle":"Chronicles of the Avatar","firstWatch":"Avatar: The Last Airbender","firstWatchKind":"series","firstWatchLabel":"Begin Book One — Water","seriesQueries":["avatar the last airbender","the legend of korra"],"movieQueries":["the last airbender"],"chips":[{"t":"3 Shows","ic":"movies"},{"t":"2 Films","ic":"movies"},{"t":"6 Novels","ic":"books"},{"t":"Comics","ic":"comics"}]},{"name":"Attack on Titan","c1":"#2a2018","category":"anime","archived":true,"blurb":"Humanity's last walls and the titans beyond them — Isayama's manga, the landmark anime, and the compilation films.","banner":"https://s4.anilist.co/file/anilistcdn/media/manga/banner/53390-6Uru5rrjh8zv.jpg","readQueries":["Attack on Titan","Attack on Titan: No Regrets","Attack on Titan: Before the Fall"],"seriesQueries":["attack on titan","attack on titan final chapters"],"movieQueries":["attack on titan"],"chips":[{"t":"3 Manga","ic":"manga"},{"t":"1 Anime","ic":"movies"},{"t":"Films","ic":"movies"}]}])J13"
;

const QVariantList &curationRows()
{
    static const QVariantList rows = [] {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(kCurationJson), &error);
        return error.error == QJsonParseError::NoError && doc.isArray()
            ? doc.array().toVariantList() : QVariantList{};
    }();
    return rows;
}

QVariantMap configFor(const QString &name)
{
    for (const QVariant &value : curationRows()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0)
            return row;
    }
    return {};
}

QString chipsLine(const QVariantMap &cfg)
{
    QStringList parts;
    for (const QVariant &value : cfg.value(QStringLiteral("chips")).toList()) {
        const QString text = value.toMap().value(QStringLiteral("t")).toString();
        if (!text.isEmpty()) parts.append(text);
    }
    return parts.join(QStringLiteral("   ·   "));
}

QString normalizedTitle(QString value)
{
    value = value.toLower();
    value.replace(QLatin1Char('&'), QStringLiteral(" and "));
    value.replace(QRegularExpression(QStringLiteral("\\bpart i\\b")), QStringLiteral("part 1"));
    value.replace(QRegularExpression(QStringLiteral("\\bpart ii\\b")), QStringLiteral("part 2"));
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return value.trimmed();
}

int firstYear(const QVariant &value)
{
    const auto match = QRegularExpression(QStringLiteral("\\d{4}")).match(value.toString());
    return match.hasMatch() ? match.captured().toInt() : 0;
}

QString biggerAppleCover(QString cover)
{
    cover.replace(QStringLiteral("100x100bb"), QStringLiteral("600x600bb"));
    cover.replace(QStringLiteral("100x100"), QStringLiteral("600x600"));
    return cover;
}

QVariantMap customSection(const QString &id, int index, const QString &state,
                          const QString &schema, QVariantMap data = {},
                          const QVariantList &items = {})
{
    QVariantMap out{{QStringLiteral("id"), id}, {QStringLiteral("index"), index},
                    {QStringLiteral("title"), QString()},
                    {QStringLiteral("layout"), QStringLiteral("custom")},
                    {QStringLiteral("state"), state},
                    {QStringLiteral("items"), items}};
    if (!schema.isEmpty()) {
        data.insert(QStringLiteral("schema"), schema);
        out.insert(QStringLiteral("data"), data);
    }
    return out;
}

QVariantMap heroSection(int index, const QString &templateName, const QVariantMap &cfg,
                        const QVariantList &items = {}, const QString &kicker = QStringLiteral("UNIVERSE"),
                        const QString &primaryLabel = QString())
{
    QVariantMap data{{QStringLiteral("template"), templateName},
                     {QStringLiteral("name"), cfg.value(QStringLiteral("name")).toString()},
                     {QStringLiteral("kicker"), kicker},
                     {QStringLiteral("blurb"), cfg.value(QStringLiteral("blurb")).toString()},
                     {QStringLiteral("banner"), cfg.value(QStringLiteral("banner")).toString()},
                     {QStringLiteral("metaline"), chipsLine(cfg)}};
    if (!cfg.value(QStringLiteral("blurbSource")).toString().isEmpty())
        data.insert(QStringLiteral("blurbSource"), cfg.value(QStringLiteral("blurbSource")));
    if (!primaryLabel.isEmpty()) data.insert(QStringLiteral("primaryLabel"), primaryLabel);
    return customSection(QStringLiteral("universe.hero"), index, QStringLiteral("ready"),
                         QStringLiteral("universes.hero"), data, items);
}

QVariantMap groupSection(const QString &id, int index, const QString &templateName,
                         const QString &label, const QVariantList &items,
                         const QString &variant = QStringLiteral("rail"),
                         const QString &note = {}, const QVariantList &locked = {},
                         bool pending = false, int ordinal = -1)
{
    QVariantMap data{{QStringLiteral("template"), templateName},
                     {QStringLiteral("label"), label},
                     {QStringLiteral("variant"), variant},
                     {QStringLiteral("pending"), pending}};
    if (!note.isEmpty()) data.insert(QStringLiteral("note"), note);
    if (!locked.isEmpty()) data.insert(QStringLiteral("locked"), locked);
    if (ordinal >= 0) data.insert(QStringLiteral("ordinal"), ordinal);
    return customSection(id, index, QStringLiteral("ready"),
                         QStringLiteral("universes.group"), data, items);
}

QVariantMap navSection(const QString &id, int index, const QString &templateName,
                       const QString &title, const QString &subtitle, const QVariantList &entries)
{
    return customSection(id, index, entries.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
        QStringLiteral("universes.nav"),
        {{QStringLiteral("template"), templateName}, {QStringLiteral("title"), title},
         {QStringLiteral("subtitle"), subtitle}, {QStringLiteral("entries"), entries}});
}

QVariantMap theatreItem(const QVariantMap &meta)
{
    const QString id = meta.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return {};
    const QString type = meta.value(QStringLiteral("type"), QStringLiteral("movie")).toString();
    QVariantMap row{{QStringLiteral("id"), id}, {QStringLiteral("tt"), id},
                    {QStringLiteral("type"), type},
                    {QStringLiteral("title"), meta.value(QStringLiteral("name"), meta.value(QStringLiteral("title"))).toString()},
                    {QStringLiteral("cover"), meta.value(QStringLiteral("poster")).toString()},
                    {QStringLiteral("backdrop"), meta.value(QStringLiteral("background")).toString()},
                    {QStringLiteral("year"), firstYear(meta.value(QStringLiteral("releaseInfo")))}};
    if (row.value(QStringLiteral("cover")).toString().isEmpty())
        row.insert(QStringLiteral("cover"), QStringLiteral("https://live.metahub.space/poster/medium/%1/img").arg(id));
    return WebFeedValue::item(row, QStringLiteral("Theatre"),
        type == QLatin1String("series") ? QStringLiteral("series") : QStringLiteral("movie"));
}

QVariantMap theatreItemFromPayload(const QVariantMap &entry)
{
    QVariantMap meta{{QStringLiteral("id"), entry.value(QStringLiteral("id"))},
                     {QStringLiteral("type"), entry.value(QStringLiteral("type"), QStringLiteral("movie"))},
                     {QStringLiteral("name"), entry.value(QStringLiteral("title"))},
                     {QStringLiteral("releaseInfo"), entry.value(QStringLiteral("year"))}};
    const QString id = entry.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
        meta.insert(QStringLiteral("poster"), QStringLiteral("https://live.metahub.space/poster/medium/%1/img").arg(id));
        meta.insert(QStringLiteral("background"), QStringLiteral("https://live.metahub.space/background/medium/%1/img").arg(id));
    }
    return theatreItem(meta);
}

QVariantMap bookItem(const QVariantMap &apple, const QString &fallbackTitle = {})
{
    QString id = apple.value(QStringLiteral("trackId"), apple.value(QStringLiteral("collectionId"))).toString();
    if (id.isEmpty()) id = apple.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return {};
    const QString title = apple.value(QStringLiteral("trackName"),
        apple.value(QStringLiteral("collectionName"), fallbackTitle)).toString();
    const QString author = apple.value(QStringLiteral("artistName"),
        apple.value(QStringLiteral("author"))).toString();
    QString cover = apple.value(QStringLiteral("artworkUrl100"),
        apple.value(QStringLiteral("cover"))).toString();
    cover = biggerAppleCover(cover);
    QVariantMap row{{QStringLiteral("id"), id}, {QStringLiteral("source"), QStringLiteral("applebooks")},
                    {QStringLiteral("title"), title.isEmpty() ? fallbackTitle : title},
                    {QStringLiteral("author"), author}, {QStringLiteral("cover"), cover},
                    {QStringLiteral("year"), firstYear(apple.value(QStringLiteral("releaseDate"),
                                                               apple.value(QStringLiteral("year"))))}};
    QVariantMap item = WebFeedValue::item(row, QStringLiteral("Biblio"), QStringLiteral("book"));
    if (!author.isEmpty()) item.insert(QStringLiteral("subtitle"), author);
    return item;
}

QVariantMap bookItemFromPayload(const QVariantMap &entry, const QVariantMap &apple = {})
{
    if (!apple.isEmpty()) return bookItem(apple, entry.value(QStringLiteral("title")).toString());
    QVariantMap row{{QStringLiteral("id"), entry.value(QStringLiteral("id"))},
                    {QStringLiteral("source"), QStringLiteral("applebooks")},
                    {QStringLiteral("title"), entry.value(QStringLiteral("title"))},
                    {QStringLiteral("author"), entry.value(QStringLiteral("author"))},
                    {QStringLiteral("year"), firstYear(entry.value(QStringLiteral("year")))}};
    QVariantMap item = WebFeedValue::item(row, QStringLiteral("Biblio"), QStringLiteral("book"));
    const QString author = entry.value(QStringLiteral("author")).toString();
    if (!author.isEmpty()) item.insert(QStringLiteral("subtitle"), author);
    return item;
}

QVariantMap mangaItemFromMal(const QVariantMap &row)
{
    if (row.isEmpty()) return {};
    QVariantMap item = WebFeedValue::item(row, QStringLiteral("Tankoban"), QStringLiteral("manga"));
    if (item.value(QStringLiteral("title")).toString().isEmpty())
        item.insert(QStringLiteral("title"), row.value(QStringLiteral("title_english"), row.value(QStringLiteral("title"))));
    return item;
}

QVariantList jsonArray(const WebFeedHttp::Reply &reply, const QString &field)
{
    if (!reply.ok || !reply.json.isObject()) return {};
    return reply.json.object().value(field).toArray().toVariantList();
}

QVariantList cinemetaSearch(const QString &kind, const QString &query)
{
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(query));
    const QUrl url(QStringLiteral("https://v3-cinemeta.strem.io/catalog/%1/top/search=%2.json")
                   .arg(kind, encoded));
    return jsonArray(WebFeedHttp::request(url), QStringLiteral("metas"));
}

QVariantList pooledCinemeta(const QString &kind, const QVariantList &queries)
{
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant &value : queries) {
        const QString query = value.toString();
        if (query.isEmpty()) continue;
        for (const QVariant &metaValue : cinemetaSearch(kind, query)) {
            const QVariantMap meta = metaValue.toMap();
            const QString id = meta.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || seen.contains(id)) continue;
            seen.insert(id);
            out.append(meta);
        }
    }
    return out;
}

QVariantList slotByCanon(const QVariantList &canon, const QVariantList &metas)
{
    QVariantList matched;
    matched.reserve(canon.size());
    for (const QVariant &value : canon) {
        const QVariantMap object = value.toMap();
        const QString wantedId = object.value(QStringLiteral("id")).toString();
        const QString wantedTitle = object.isEmpty() ? value.toString()
            : object.value(QStringLiteral("t")).toString();
        QVariantMap hit;
        for (const QVariant &metaValue : metas) {
            const QVariantMap meta = metaValue.toMap();
            if ((!wantedId.isEmpty() && meta.value(QStringLiteral("id")).toString() == wantedId)
                || (wantedId.isEmpty() && normalizedTitle(meta.value(QStringLiteral("name"),
                        meta.value(QStringLiteral("title"))).toString()) == normalizedTitle(wantedTitle))) {
                hit = meta;
                break;
            }
        }
        matched.append(hit);
    }
    return matched;
}

QVariantMap appleSearchFirst(const QString &query)
{
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(query));
    const QUrl url(QStringLiteral("https://itunes.apple.com/search?media=ebook&limit=24&term=%1").arg(encoded));
    const QVariantList results = jsonArray(WebFeedHttp::request(url), QStringLiteral("results"));
    return results.isEmpty() ? QVariantMap{} : results.first().toMap();
}

QHash<QString, QVariantMap> appleLookupIds(const QStringList &ids)
{
    QHash<QString, QVariantMap> out;
    if (ids.isEmpty()) return out;
    QStringList unique;
    QSet<QString> seen;
    for (const QString &id : ids) if (!id.isEmpty() && !seen.contains(id)) { seen.insert(id); unique.append(id); }
    for (int start = 0; start < unique.size(); start += 100) {
        const QStringList chunk = unique.mid(start, 100);
        const QUrl url(QStringLiteral("https://itunes.apple.com/lookup?id=%1").arg(chunk.join(QLatin1Char(','))));
        for (const QVariant &value : jsonArray(WebFeedHttp::request(url), QStringLiteral("results"))) {
            const QVariantMap row = value.toMap();
            const QString id = row.value(QStringLiteral("trackId"), row.value(QStringLiteral("collectionId"))).toString();
            if (!id.isEmpty()) out.insert(id, row);
        }
    }
    return out;
}

QHash<QString, QString> getComicsCovers(const QStringList &postIds)
{
    QHash<QString, QString> out;
    if (postIds.isEmpty()) return out;
    QStringList unique;
    QSet<QString> seen;
    for (const QString &id : postIds) if (!id.isEmpty() && !seen.contains(id)) { seen.insert(id); unique.append(id); }
    for (int start = 0; start < unique.size(); start += 100) {
        const QStringList chunk = unique.mid(start, 100);
        const QUrl url(QStringLiteral("https://getcomics.org/wp-json/wp/v2/posts?include=%1&per_page=%2&_fields=id,yoast_head_json")
                       .arg(chunk.join(QLatin1Char(','))).arg(chunk.size()));
        const WebFeedHttp::Reply reply = WebFeedHttp::request(url);
        if (!reply.ok || !reply.json.isArray()) continue;
        for (const QJsonValue &value : reply.json.array()) {
            const QJsonObject object = value.toObject();
            const QString id = QString::number(object.value(QStringLiteral("id")).toInt());
            const QJsonArray images = object.value(QStringLiteral("yoast_head_json")).toObject()
                                      .value(QStringLiteral("og_image")).toArray();
            if (!images.isEmpty()) {
                const QString cover = images.first().toObject().value(QStringLiteral("url")).toString();
                if (!id.isEmpty() && !cover.isEmpty()) out.insert(id, cover);
            }
        }
    }
    return out;
}

QVariantMap payloadSection(const QVariantMap &payload, const QString &id)
{
    for (const QVariant &value : payload.value(QStringLiteral("sections")).toList()) {
        const QVariantMap section = value.toMap();
        if (section.value(QStringLiteral("id")).toString() == id) return section;
    }
    return {};
}

QVariantMap malMatch(MalCatalog *catalog, const QString &title)
{
    if (!catalog || !catalog->ready() || title.isEmpty()) return {};
    const QVariantList rows = catalog->search(title, 8, QStringLiteral("manga"));
    const QString wanted = normalizedTitle(title);
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString english = row.value(QStringLiteral("title_english")).toString();
        const QString primary = row.value(QStringLiteral("title")).toString();
        if (normalizedTitle(english) == wanted || normalizedTitle(primary) == wanted) return row;
    }
    return rows.isEmpty() ? QVariantMap{} : rows.first().toMap();
}

QVariantMap lockedRecord(const QVariantMap &entry, const QString &cover = {})
{
    QVariantMap row{{QStringLiteral("key"), entry.value(QStringLiteral("id"), entry.value(QStringLiteral("title"))).toString()},
                    {QStringLiteral("title"), entry.value(QStringLiteral("title")).toString()}};
    const QString year = entry.value(QStringLiteral("year")).toString();
    const QString note = entry.value(QStringLiteral("note")).toString();
    QStringList subtitle;
    if (!year.isEmpty()) subtitle.append(year);
    if (!note.isEmpty()) subtitle.append(note);
    if (!subtitle.isEmpty()) row.insert(QStringLiteral("subtitle"), subtitle.join(QStringLiteral(" · ")));
    if (!cover.isEmpty()) row.insert(QStringLiteral("cover"), cover);
    return row;
}

QVariantMap payloadItem(const QVariantMap &entry, const QString &kind,
                        const QHash<QString, QVariantMap> &appleRows,
                        MalCatalog *mal, const QHash<QString, QString> &comicCovers,
                        QVariantMap *locked = nullptr)
{
    if (kind == QLatin1String("video")) return theatreItemFromPayload(entry);
    if (kind == QLatin1String("book")) {
        const QString id = entry.value(QStringLiteral("id")).toString();
        return bookItemFromPayload(entry, appleRows.value(id));
    }
    if (kind == QLatin1String("manga")) {
        const QVariantMap match = malMatch(mal, entry.value(QStringLiteral("title")).toString());
        if (!match.isEmpty()) return mangaItemFromMal(match);
        if (locked) *locked = lockedRecord(entry,
            entry.value(QStringLiteral("provider")).toString() == QLatin1String("weebcentral")
                ? QStringLiteral("https://temp.compsci88.com/cover/fallback/%1.jpg")
                    .arg(entry.value(QStringLiteral("id")).toString()) : QString());
        return {};
    }
    if (kind == QLatin1String("comic")) {
        const QString gcdId = entry.value(QStringLiteral("gcdId")).toString();
        if (!gcdId.isEmpty() && gcdId != QLatin1String("0")) {
            QVariantMap row{{QStringLiteral("id"), QStringLiteral("gcd:") + gcdId},
                            {QStringLiteral("gcdId"), gcdId},
                            {QStringLiteral("kind"), QStringLiteral("comic")},
                            {QStringLiteral("title"), entry.value(QStringLiteral("title"))},
                            {QStringLiteral("cover"), entry.value(QStringLiteral("cover"))},
                            {QStringLiteral("year"), firstYear(entry.value(QStringLiteral("year")))}};
            return WebFeedValue::item(row, QStringLiteral("Tankoban"), QStringLiteral("comic"));
        }
        const QVariantList posts = entry.value(QStringLiteral("posts")).toList();
        const QString first = posts.isEmpty() ? QString() : posts.first().toString();
        if (locked) *locked = lockedRecord(entry, comicCovers.value(first));
        return {};
    }
    return {};
}

QVariantList payloadItems(const QVariantMap &payloadSection,
                          const QHash<QString, QVariantMap> &appleRows = {},
                          MalCatalog *mal = nullptr,
                          const QHash<QString, QString> &comicCovers = {},
                          QVariantList *locked = nullptr)
{
    QVariantList items;
    const QString kind = payloadSection.value(QStringLiteral("kind")).toString();
    for (const QVariant &value : payloadSection.value(QStringLiteral("entries")).toList()) {
        QVariantMap blocked;
        const QVariantMap item = payloadItem(value.toMap(), kind, appleRows, mal, comicCovers, &blocked);
        if (!item.isEmpty()) items.append(item);
        else if (locked && !blocked.isEmpty()) locked->append(blocked);
    }
    return items;
}

QStringList payloadAppleIds(const QVariantMap &payload)
{
    QStringList ids;
    for (const QVariant &sectionValue : payload.value(QStringLiteral("sections")).toList()) {
        const QVariantMap section = sectionValue.toMap();
        if (section.value(QStringLiteral("kind")).toString() != QLatin1String("book")) continue;
        for (const QVariant &entryValue : section.value(QStringLiteral("entries")).toList()) {
            const QString id = entryValue.toMap().value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) ids.append(id);
        }
    }
    return ids;
}

QStringList payloadComicPosts(const QVariantMap &payload)
{
    QStringList ids;
    for (const QVariant &sectionValue : payload.value(QStringLiteral("sections")).toList()) {
        const QVariantMap section = sectionValue.toMap();
        if (section.value(QStringLiteral("kind")).toString() != QLatin1String("comic")) continue;
        for (const QVariant &entryValue : section.value(QStringLiteral("entries")).toList()) {
            const QVariantList posts = entryValue.toMap().value(QStringLiteral("posts")).toList();
            if (!posts.isEmpty()) ids.append(posts.first().toString());
        }
    }
    return ids;
}

bool valid(const QVariantMap &params)
{
    const QString extensionId = params.value(QStringLiteral("extensionId")).toString();
    return !extensionId.isEmpty() && extensionId != QLatin1String(kOnePiece);
}

QVariantList initial(const QVariantMap &)
{
    return {customSection(QStringLiteral("universe.hero"), 0, QStringLiteral("loading"), {})};
}

// UniverseExtApi.js payload seam; DCAUUniversePage.qml:33-37 and GalaxyUniversePage.qml payload boot.
void capture(ColosseumWebBridge &bridge, FeedContext &context)
{
    const QString id = context.params.value(QStringLiteral("extensionId")).toString();
    if (id != QLatin1String(kDcau) && id != QLatin1String(kStarWars)) return;
    auto *extensions = qobject_cast<ExtensionsStore *>(bridge.service(QStringLiteral("Extensions")));
    if (!extensions) return;
    const QString stem = id == QLatin1String(kDcau) ? QStringLiteral("dcau") : QStringLiteral("star-wars");
    const QByteArray json = extensions->universePayload(stem).toUtf8();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error == QJsonParseError::NoError && doc.isObject())
        context.nativeSnapshot.insert(QStringLiteral("payload"),
            doc.object().value(QStringLiteral("universe")).toObject().toVariantMap());
}

QVariantList dcauHubs()
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("gotham")}, {QStringLiteral("title"), QStringLiteral("Gotham")},
                    {QStringLiteral("portalImdb"), QStringLiteral("tt0103359")},
                    {QStringLiteral("videoIds"), QStringList{QStringLiteral("tt0103359"),QStringLiteral("tt0106364"),QStringLiteral("tt0118266"),QStringLiteral("tt0143127"),QStringLiteral("tt0337763"),QStringLiteral("tt0346578"),QStringLiteral("tt6556890")}},
                    {QStringLiteral("comicPosts"), QStringList{QStringLiteral("11366"),QStringLiteral("153724"),QStringLiteral("50187"),QStringLiteral("15941"),QStringLiteral("10470"),QStringLiteral("183948"),QStringLiteral("80956")}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("metropolis")}, {QStringLiteral("title"), QStringLiteral("Metropolis")},
                    {QStringLiteral("portalImdb"), QStringLiteral("tt0115378")},
                    {QStringLiteral("videoIds"), QStringList{QStringLiteral("tt0115378"),QStringLiteral("tt6075386")}},
                    {QStringLiteral("comicPosts"), QStringList{QStringLiteral("14615")}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("justice")}, {QStringLiteral("title"), QStringLiteral("Watch Tower")},
                    {QStringLiteral("portalImdb"), QStringLiteral("tt0275137")},
                    {QStringLiteral("videoIds"), QStringList{QStringLiteral("tt0247729"),QStringLiteral("tt0275137"),QStringLiteral("tt6025022"),QStringLiteral("tt8752474")}},
                    {QStringLiteral("comicPosts"), QStringList{QStringLiteral("48881"),QStringLiteral("10563"),QStringLiteral("8823")}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("future")}, {QStringLiteral("title"), QStringLiteral("Future Gotham")},
                    {QStringLiteral("portalImdb"), QStringLiteral("tt0147746")},
                    {QStringLiteral("videoIds"), QStringList{QStringLiteral("tt0147746"),QStringLiteral("tt0231237"),QStringLiteral("tt0233298"),QStringLiteral("tt0260662")}},
                    {QStringLiteral("comicPosts"), QStringList{QStringLiteral("190572"),QStringLiteral("163954"),QStringLiteral("282726")}}}
    };
}

QVariantList filterPayloadEntries(const QVariantMap &section, const QStringList &ids, bool byPosts)
{
    QVariantList out;
    for (const QVariant &value : section.value(QStringLiteral("entries")).toList()) {
        const QVariantMap entry = value.toMap();
        if (!byPosts) {
            if (ids.contains(entry.value(QStringLiteral("id")).toString())) out.append(entry);
            continue;
        }
        bool keep = false;
        for (const QVariant &post : entry.value(QStringLiteral("posts")).toList())
            if (ids.contains(post.toString())) { keep = true; break; }
        if (keep) out.append(entry);
    }
    return out;
}

QVariantMap dcauContinue(const FeedContext &context, int index)
{
    const QVariantList built = ContinueFeed::build(context.recent, QStringLiteral("all"),
                                                   context.paths.imdb, 100);
    if (built.isEmpty()) return {};
    const QStringList videos{QStringLiteral("tt0247729"),QStringLiteral("tt0275137"),
                             QStringLiteral("tt6025022"),QStringLiteral("tt8752474")};
    const QStringList comics{QStringLiteral("gcd:5719"),QStringLiteral("gcd:10924"),
                             QStringLiteral("gcd:11988")};
    QVariantList items;
    for (const QVariant &value : built.first().toMap().value(QStringLiteral("items")).toList()) {
        const QVariantMap item = value.toMap();
        const QVariantMap ref = item.value(QStringLiteral("ref")).toMap();
        const QString stored = ref.value(QStringLiteral("kind")).toString();
        const QString id = ref.value(QStringLiteral("id")).toString();
        const QString root = id.section(QLatin1Char(':'), 0, 0);
        if ((stored == QLatin1String("video") && videos.contains(root))
            || (stored == QLatin1String("comic") && comics.contains(id)))
            items.append(item);
        if (items.size() >= 12) break;
    }
    if (items.isEmpty()) return {};
    return WebFeedValue::section(QStringLiteral("universe.dcau.continue"), index,
        QStringLiteral("Continue"), QStringLiteral("continue"), items, QStringLiteral("ready"));
}

// DCAUUniversePage.qml:17-49,85-209 + DCAUWorldPage.qml:12-37,51-129 + DCAUUniverseData.js:15-55.
QVariantList buildDcau(const FeedContext &context)
{
    const QVariantMap payload = context.nativeSnapshot.value(QStringLiteral("payload")).toMap();
    if (payload.isEmpty()) {
        QVariantMap error = customSection(QStringLiteral("universe.dcau.error"), 0, QStringLiteral("error"), {});
        error.insert(QStringLiteral("error"), QStringLiteral("This universe isn't installed yet."));
        return {error};
    }
    QVariantList sections;
    if (const QVariantMap continuing = dcauContinue(context, sections.size()); !continuing.isEmpty())
        sections.append(continuing);

    QVariantList destinations;
    for (const QVariant &hubValue : dcauHubs()) {
        const QVariantMap hub = hubValue.toMap();
        const QString id = hub.value(QStringLiteral("id")).toString();
        const QString poster = QStringLiteral("https://live.metahub.space/background/medium/%1/img")
                               .arg(hub.value(QStringLiteral("portalImdb")).toString());
        destinations.append(QVariantMap{
            {QStringLiteral("key"), id},
            {QStringLiteral("label"), hub.value(QStringLiteral("title"))},
            {QStringLiteral("targetId"), QStringLiteral("universe.dcau.%1.tankoban").arg(id)},
            {QStringLiteral("art"), poster}});
    }
    sections.append(navSection(QStringLiteral("universe.dcau.portals"), sections.size(),
        QStringLiteral("dcau"), QStringLiteral("DC Animated Universe"), QString(), destinations));

    const QVariantMap videos = payloadSection(payload, QStringLiteral("tv"));
    const QVariantMap shorts = payloadSection(payload, QStringLiteral("shorts"));
    const QVariantMap movies = payloadSection(payload, QStringLiteral("movies"));
    QVariantMap allVideos{{QStringLiteral("kind"), QStringLiteral("video")}};
    QVariantList videoEntries = videos.value(QStringLiteral("entries")).toList();
    videoEntries.append(shorts.value(QStringLiteral("entries")).toList());
    videoEntries.append(movies.value(QStringLiteral("entries")).toList());
    allVideos.insert(QStringLiteral("entries"), videoEntries);
    const QVariantMap allComics = payloadSection(payload, QStringLiteral("comics"));

    for (const QVariant &hubValue : dcauHubs()) {
        const QVariantMap hub = hubValue.toMap();
        const QString id = hub.value(QStringLiteral("id")).toString();
        QVariantMap comicSection{{QStringLiteral("kind"), QStringLiteral("comic")},
            {QStringLiteral("entries"), filterPayloadEntries(allComics,
                 hub.value(QStringLiteral("comicPosts")).toStringList(), true)}};
        QVariantMap videoSection{{QStringLiteral("kind"), QStringLiteral("video")},
            {QStringLiteral("entries"), filterPayloadEntries(allVideos,
                 hub.value(QStringLiteral("videoIds")).toStringList(), false)}};
        QVariantList comicLocked;
        const QVariantList comicItems = payloadItems(comicSection, {}, nullptr, {}, &comicLocked);
        const QVariantList theatreItems = payloadItems(videoSection);
        sections.append(groupSection(QStringLiteral("universe.dcau.%1.tankoban").arg(id),
            sections.size(), QStringLiteral("dcau"), QStringLiteral("Tankoban"), comicItems,
            QStringLiteral("rail"), hub.value(QStringLiteral("title")).toString(), comicLocked));
        sections.append(groupSection(QStringLiteral("universe.dcau.%1.theatre").arg(id),
            sections.size(), QStringLiteral("dcau"), QStringLiteral("Theatre"), theatreItems,
            QStringLiteral("rail"), hub.value(QStringLiteral("title")).toString()));
    }
    return sections;
}

QVariantMap swShelf(const QStringList &ids, const QString &medium, const QString &note)
{
    return {{QStringLiteral("ids"), ids}, {QStringLiteral("medium"), medium},
            {QStringLiteral("note"), note}};
}

QVariantList starWarsDestinations()
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("skywalker")}, {QStringLiteral("label"), QStringLiteral("SKYWALKER SAGA")},
                    {QStringLiteral("world"), QStringLiteral("EPISODES I–IX")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("skywalker-saga-screen")}, QStringLiteral("Theatre"), QStringLiteral("Episodes I–IX"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("high")}, {QStringLiteral("label"), QStringLiteral("HIGH REPUBLIC")},
                    {QStringLiteral("world"), QStringLiteral("VALO")}, {QStringLiteral("artId"), QStringLiteral("tt20674124")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("high-republic-screen")}, QStringLiteral("Theatre"), QStringLiteral("High Republic series")),
                        swShelf({QStringLiteral("high-republic-books"),QStringLiteral("high-republic-ya")}, QStringLiteral("Biblio"), QStringLiteral("Canon novels + young adult")),
                        swShelf({QStringLiteral("high-republic-comics")}, QStringLiteral("Tankoban"), QStringLiteral("Collected comic lines"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("fall")}, {QStringLiteral("label"), QStringLiteral("FALL OF THE JEDI")},
                    {QStringLiteral("world"), QStringLiteral("CORUSCANT")}, {QStringLiteral("artId"), QStringLiteral("tt0120915")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("fall-of-the-jedi-screen")}, QStringLiteral("Theatre"), QStringLiteral("Republic era")),
                        swShelf({QStringLiteral("republic-books")}, QStringLiteral("Biblio"), QStringLiteral("Republic era novels")),
                        swShelf({QStringLiteral("fall-empire-comics")}, QStringLiteral("Tankoban"), QStringLiteral("Spans Fall of the Jedi + Reign of the Empire"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("empire")}, {QStringLiteral("label"), QStringLiteral("REIGN OF THE EMPIRE")},
                    {QStringLiteral("world"), QStringLiteral("MUSTAFAR")}, {QStringLiteral("artId"), QStringLiteral("tt0121766")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("reign-of-the-empire-screen")}, QStringLiteral("Theatre"), QStringLiteral("Imperial era")),
                        swShelf({QStringLiteral("imperial-books")}, QStringLiteral("Biblio"), QStringLiteral("Imperial era novels")),
                        swShelf({QStringLiteral("fall-empire-comics")}, QStringLiteral("Tankoban"), QStringLiteral("Spans Fall of the Jedi + Reign of the Empire"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("rebellion")}, {QStringLiteral("label"), QStringLiteral("AGE OF REBELLION")},
                    {QStringLiteral("world"), QStringLiteral("HOTH")}, {QStringLiteral("artId"), QStringLiteral("tt0080684")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("age-of-rebellion-screen")}, QStringLiteral("Theatre"), QStringLiteral("Rebellion era")),
                        swShelf({QStringLiteral("rebellion-new-republic-books")}, QStringLiteral("Biblio"), QStringLiteral("Spans Age of Rebellion + New Republic")),
                        swShelf({QStringLiteral("rebellion-comics")}, QStringLiteral("Tankoban"), QStringLiteral("Rebellion era comics"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("newrep")}, {QStringLiteral("label"), QStringLiteral("NEW REPUBLIC")},
                    {QStringLiteral("world"), QStringLiteral("NEVARRO")}, {QStringLiteral("artId"), QStringLiteral("tt8111088")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("new-republic-screen")}, QStringLiteral("Theatre"), QStringLiteral("New Republic era")),
                        swShelf({QStringLiteral("rebellion-new-republic-books")}, QStringLiteral("Biblio"), QStringLiteral("Spans Age of Rebellion + New Republic")),
                        swShelf({QStringLiteral("new-republic-first-order-comics")}, QStringLiteral("Tankoban"), QStringLiteral("Spans New Republic + Rise of the First Order"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("firstorder")}, {QStringLiteral("label"), QStringLiteral("RISE OF THE FIRST ORDER")},
                    {QStringLiteral("world"), QStringLiteral("JAKKU")}, {QStringLiteral("artId"), QStringLiteral("tt2488496")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("rise-first-order-screen")}, QStringLiteral("Theatre"), QStringLiteral("First Order era")),
                        swShelf({QStringLiteral("first-order-books")}, QStringLiteral("Biblio"), QStringLiteral("First Order era novels")),
                        swShelf({QStringLiteral("new-republic-first-order-comics")}, QStringLiteral("Tankoban"), QStringLiteral("Spans New Republic + Rise of the First Order"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("beyond")}, {QStringLiteral("label"), QStringLiteral("BEYOND THE SKYWALKER SAGA")},
                    {QStringLiteral("world"), QStringLiteral("AHCH-TO")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("beyond-skywalker-screen")}, QStringLiteral("Theatre"), QStringLiteral("Beyond the Skywalker Saga"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("across")}, {QStringLiteral("label"), QStringLiteral("ACROSS THE ERAS")},
                    {QStringLiteral("world"), QStringLiteral("CANON · CROSS-ERA")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("canon-anthologies-screen")}, QStringLiteral("Theatre"), QStringLiteral("Canon anthologies")),
                        swShelf({QStringLiteral("young-adult-books")}, QStringLiteral("Biblio"), QStringLiteral("Young adult stories across eras"))}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("outside")}, {QStringLiteral("label"), QStringLiteral("BEYOND CANON")},
                    {QStringLiteral("world"), QStringLiteral("ADJACENT CONTINUITIES")},
                    {QStringLiteral("shelves"), QVariantList{swShelf({QStringLiteral("visions-screen"),QStringLiteral("vintage-screen"),QStringLiteral("lego-screen")}, QStringLiteral("Theatre"), QStringLiteral("Visions · Vintage & Legends · LEGO")),
                        swShelf({QStringLiteral("visions-manga")}, QStringLiteral("Tankoban"), QStringLiteral("Visions manga"))}}}
    };
}

QVariantMap mergePayloadSections(const QVariantMap &payload, const QStringList &ids)
{
    QVariantMap merged;
    QVariantList entries;
    QString kind;
    for (const QString &id : ids) {
        const QVariantMap section = payloadSection(payload, id);
        if (section.isEmpty()) continue;
        if (kind.isEmpty()) kind = section.value(QStringLiteral("kind")).toString();
        entries.append(section.value(QStringLiteral("entries")).toList());
    }
    merged.insert(QStringLiteral("kind"), kind);
    merged.insert(QStringLiteral("entries"), entries);
    return merged;
}

// GalaxyUniversePage.qml + StarWarsGalaxySystem.qml:46-220 + SagaApi.js:189-258.
QVariantList buildStarWars(const FeedContext &context, bool enrich)
{
    const QVariantMap payload = context.nativeSnapshot.value(QStringLiteral("payload")).toMap();
    if (payload.isEmpty()) {
        QVariantMap error = customSection(QStringLiteral("universe.starwars.error"), 0, QStringLiteral("error"), {});
        error.insert(QStringLiteral("error"), QStringLiteral("This universe isn't installed yet."));
        return {error};
    }

    QHash<QString, QVariantMap> appleRows;
    QHash<QString, QString> comicCovers;
    std::unique_ptr<MalCatalog> mal;
    if (enrich) {
        appleRows = appleLookupIds(payloadAppleIds(payload));
        comicCovers = getComicsCovers(payloadComicPosts(payload));
        mal = std::make_unique<MalCatalog>(context.paths.mal, nullptr,
            QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    QVariantMap cfg = configFor(QStringLiteral("Star Wars"));
    if (!payload.value(QStringLiteral("background")).toString().isEmpty())
        cfg.insert(QStringLiteral("banner"), payload.value(QStringLiteral("background")));
    QVariantList sections{heroSection(0, QStringLiteral("galaxy"), cfg, {},
        QStringLiteral("A LONG TIME AGO IN A GALAXY FAR, FAR AWAY"))};

    QVariantList nav;
    for (const QVariant &value : starWarsDestinations()) {
        const QVariantMap destination = value.toMap();
        const QVariantList shelves = destination.value(QStringLiteral("shelves")).toList();
        const QString target = shelves.isEmpty() ? QStringLiteral("universe.hero")
            : QStringLiteral("universe.starwars.%1.0").arg(destination.value(QStringLiteral("id")).toString());
        QVariantMap entry{{QStringLiteral("key"), destination.value(QStringLiteral("id"))},
                          {QStringLiteral("label"), destination.value(QStringLiteral("label"))},
                          {QStringLiteral("sublabel"), destination.value(QStringLiteral("world"))},
                          {QStringLiteral("targetId"), target}};
        const QString artId = destination.value(QStringLiteral("artId")).toString();
        if (!artId.isEmpty()) entry.insert(QStringLiteral("art"),
            QStringLiteral("https://live.metahub.space/background/medium/%1/img").arg(artId));
        nav.append(entry);
    }
    sections.append(navSection(QStringLiteral("universe.starwars.galaxy"), sections.size(),
        QStringLiteral("galaxy"), QStringLiteral("THE GALAXY"),
        QStringLiteral("Choose an era of the canon."), nav));

    for (const QVariant &value : starWarsDestinations()) {
        const QVariantMap destination = value.toMap();
        const QString destinationId = destination.value(QStringLiteral("id")).toString();
        int shelfIndex = 0;
        for (const QVariant &shelfValue : destination.value(QStringLiteral("shelves")).toList()) {
            const QVariantMap shelf = shelfValue.toMap();
            const QVariantMap merged = mergePayloadSections(payload, shelf.value(QStringLiteral("ids")).toStringList());
            QVariantList locked;
            const QVariantList items = payloadItems(merged, appleRows, mal.get(), comicCovers, &locked);
            sections.append(groupSection(
                QStringLiteral("universe.starwars.%1.%2").arg(destinationId).arg(shelfIndex++),
                sections.size(), QStringLiteral("galaxy"), shelf.value(QStringLiteral("medium")).toString(),
                items, QStringLiteral("rail"),
                destination.value(QStringLiteral("label")).toString() + QStringLiteral(" · ")
                    + shelf.value(QStringLiteral("note")).toString(),
                locked, !enrich && merged.value(QStringLiteral("kind")).toString() == QLatin1String("manga")));
        }
    }
    return sections;
}

QHash<QString, QVariantMap> resolveCosmereBooks(const QVariantMap &cfg)
{
    QHash<QString, QVariantMap> resolved;
    QSet<QString> queries;
    auto collect = [&queries](const QVariantList &entries) {
        for (const QVariant &value : entries) {
            const QString query = value.toMap().value(QStringLiteral("query")).toString();
            if (!query.isEmpty()) queries.insert(query);
        }
    };
    collect(cfg.value(QStringLiteral("cosmereStarters")).toList());
    for (const QVariant &value : cfg.value(QStringLiteral("cosmereWorlds")).toList())
        collect(value.toMap().value(QStringLiteral("books")).toList());
    for (const QVariant &value : cfg.value(QStringLiteral("cosmereSeries")).toList())
        collect(value.toMap().value(QStringLiteral("books")).toList());
    for (const QString &query : std::as_const(queries)) {
        const QVariantMap book = appleSearchFirst(query);
        if (!book.isEmpty()
            && book.value(QStringLiteral("artistName")).toString().contains(
                QStringLiteral("Brandon Sanderson"), Qt::CaseInsensitive))
            resolved.insert(query, book);
    }
    return resolved;
}

QVariantList cosmereItems(const QVariantList &declared,
                          const QHash<QString, QVariantMap> &resolved)
{
    QVariantList items;
    for (const QVariant &value : declared) {
        const QVariantMap spec = value.toMap();
        const QString query = spec.value(QStringLiteral("query")).toString();
        const QVariantMap book = resolved.value(query);
        if (book.isEmpty()) continue;
        QVariantMap item = bookItem(book);
        if (item.isEmpty()) continue;
        const QString label = spec.value(QStringLiteral("label")).toString();
        if (!label.isEmpty()) item.insert(QStringLiteral("badge"), label);
        items.append(item);
    }
    return items;
}

// CosmereUniversePage.qml:156-340 + CosmereApi.js:8-96.
QVariantList buildCosmere(const QVariantMap &cfg, bool enrich)
{
    const QHash<QString, QVariantMap> resolved = enrich ? resolveCosmereBooks(cfg)
                                                       : QHash<QString, QVariantMap>{};
    QVariantList sections;
    sections.append(heroSection(0, QStringLiteral("cosmere"), cfg, {},
        QStringLiteral("THE COSMERE  /  A CONNECTED EPIC")));

    QVariantList starterItems;
    QVariantList starterEntries;
    for (const QVariant &value : cfg.value(QStringLiteral("cosmereStarters")).toList()) {
        const QVariantMap spec = value.toMap();
        const QString query = spec.value(QStringLiteral("query")).toString();
        QVariantMap data{{QStringLiteral("key"), query},
                         {QStringLiteral("short"), spec.value(QStringLiteral("short"))},
                         {QStringLiteral("label"), spec.value(QStringLiteral("label"))},
                         {QStringLiteral("note"), spec.value(QStringLiteral("note"))}};
        const QVariantMap resolvedBook = resolved.value(query);
        if (!resolvedBook.isEmpty()) {
            QVariantMap item = bookItem(resolvedBook);
            if (!item.isEmpty()) {
                data.insert(QStringLiteral("itemKey"), item.value(QStringLiteral("key")));
                starterItems.append(item);
            }
        }
        starterEntries.append(data);
    }
    sections.append(customSection(QStringLiteral("universe.cosmere.starters"), sections.size(),
        QStringLiteral("ready"), QStringLiteral("universes.starters"),
        {{QStringLiteral("title"), QStringLiteral("BEGIN HERE")},
         {QStringLiteral("entries"), starterEntries}}, starterItems));

    QVariantList nav;
    int worldOrdinal = 0;
    for (const QVariant &value : cfg.value(QStringLiteral("cosmereWorlds")).toList()) {
        const QVariantMap world = value.toMap();
        const QString id = QStringLiteral("universe.cosmere.world.%1").arg(worldOrdinal);
        nav.append(QVariantMap{{QStringLiteral("key"), QString::number(worldOrdinal)},
                               {QStringLiteral("label"), world.value(QStringLiteral("name"))},
                               {QStringLiteral("sublabel"), world.value(QStringLiteral("epithet"))},
                               {QStringLiteral("targetId"), id}});
        ++worldOrdinal;
    }
    sections.append(navSection(QStringLiteral("universe.cosmere.atlas"), sections.size(),
        QStringLiteral("cosmere"), QStringLiteral("THE COGNITIVE ATLAS"),
        QStringLiteral("Six systems. One underlying light."), nav));

    worldOrdinal = 0;
    for (const QVariant &value : cfg.value(QStringLiteral("cosmereWorlds")).toList()) {
        const QVariantMap world = value.toMap();
        sections.append(groupSection(QStringLiteral("universe.cosmere.world.%1").arg(worldOrdinal++),
            sections.size(), QStringLiteral("cosmere"), world.value(QStringLiteral("name")).toString(),
            cosmereItems(world.value(QStringLiteral("books")).toList(), resolved), QStringLiteral("rail"),
            world.value(QStringLiteral("epithet")).toString(), {}, !enrich));
    }

    int seriesOrdinal = 0;
    for (const QVariant &value : cfg.value(QStringLiteral("cosmereSeries")).toList()) {
        const QVariantMap series = value.toMap();
        sections.append(groupSection(QStringLiteral("universe.cosmere.series.%1").arg(seriesOrdinal++),
            sections.size(), QStringLiteral("cosmere"), series.value(QStringLiteral("name")).toString(),
            cosmereItems(series.value(QStringLiteral("books")).toList(), resolved), QStringLiteral("rail"),
            series.value(QStringLiteral("epithet")).toString(), {}, !enrich));
    }
    return sections;
}


QVariantList cinemetaCanonItems(const QVariantList &canon, const QVariantList &queries,
                                const QString &kind)
{
    QVariantList effectiveQueries = queries;
    if (effectiveQueries.isEmpty()) {
        for (const QVariant &value : canon) {
            const QVariantMap object = value.toMap();
            const QString title = object.isEmpty() ? value.toString() : object.value(QStringLiteral("t")).toString();
            if (!title.isEmpty()) effectiveQueries.append(title);
        }
    }
    const QVariantList matched = slotByCanon(canon, pooledCinemeta(kind, effectiveQueries));
    QVariantList items;
    for (const QVariant &value : matched) {
        const QVariantMap meta = value.toMap();
        if (!meta.isEmpty()) items.append(theatreItem(meta));
    }
    return items;
}

QVariantList appleBookItems(const QVariantList &titles)
{
    QVariantList items;
    for (const QVariant &value : titles) {
        const QVariantMap object = value.toMap();
        const QString title = object.isEmpty() ? value.toString() : object.value(QStringLiteral("t")).toString();
        if (title.isEmpty()) continue;
        const QVariantMap item = bookItem(appleSearchFirst(title), title);
        if (!item.isEmpty()) items.append(item);
    }
    return items;
}

QVariantList mangaQueryItems(const QVariantList &queries, const WorldFeed::Paths &paths)
{
    QVariantList items;
    QSet<QString> seen;
    MalCatalog catalog(paths.mal, nullptr, QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!catalog.ready()) return items;
    const int each = queries.size() > 1 ? 2 : 14;
    for (const QVariant &value : queries) {
        const QString query = value.toString();
        if (query.isEmpty()) continue;
        int taken = 0;
        for (const QVariant &rowValue : catalog.search(query, 14, QStringLiteral("manga"))) {
            const QVariantMap row = rowValue.toMap();
            const QString mal = row.value(QStringLiteral("mal_id")).toString();
            if (mal.isEmpty() || seen.contains(mal)) continue;
            seen.insert(mal);
            const QVariantMap item = mangaItemFromMal(row);
            if (!item.isEmpty()) items.append(item);
            if (++taken >= each || items.size() >= 20) break;
        }
        if (items.size() >= 20) break;
    }
    return items;
}

QVariantMap comicDoorItem(const QVariantMap &cfg)
{
    const QVariantMap comics = cfg.value(QStringLiteral("comics")).toMap();
    const QString tag = comics.value(QStringLiteral("tag")).toString();
    if (tag.isEmpty()) return {};
    const QString title = comics.value(QStringLiteral("line"),
        QStringLiteral("The canon continues in print.")).toString();
    QVariantMap row{{QStringLiteral("id"), QStringLiteral("gc:") + tag},
                    {QStringLiteral("kind"), QStringLiteral("comic")},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("cover"), cfg.value(QStringLiteral("banner"))}};
    QVariantMap item = WebFeedValue::item(row, QStringLiteral("Tankoban"), QStringLiteral("comic"));
    item.insert(QStringLiteral("badge"), QStringLiteral("GETCOMICS ARCHIVE"));
    return item;
}

QVariantMap dualitySection(int index, const QString &templateName,
                           const QVariantMap &left, const QString &leftLabel, const QString &leftSub,
                           const QVariantMap &right, const QString &rightLabel, const QString &rightSub)
{
    QVariantList items;
    if (!left.isEmpty()) items.append(left);
    if (!right.isEmpty()) items.append(right);
    QVariantMap data{{QStringLiteral("template"), templateName},
                     {QStringLiteral("leftLabel"), leftLabel},
                     {QStringLiteral("leftSub"), leftSub},
                     {QStringLiteral("leftKey"), left.value(QStringLiteral("key"))},
                     {QStringLiteral("rightLabel"), rightLabel},
                     {QStringLiteral("rightSub"), rightSub},
                     {QStringLiteral("rightKey"), right.value(QStringLiteral("key"))}};
    return customSection(QStringLiteral("universe.duality"), index, QStringLiteral("ready"),
                         QStringLiteral("universes.duality"), data, items);
}

QVariantList numbered(QVariantList items)
{
    for (int i = 0; i < items.size(); ++i) {
        QVariantMap item = items.at(i).toMap();
        item.insert(QStringLiteral("badge"), QString::number(i + 1));
        items[i] = item;
    }
    return items;
}

// SagaUniversePage.qml page body + SagaApi.js:113-188.
QVariantList buildSaga(const QVariantMap &cfg, bool enrich)
{
    const QVariantList books = enrich ? numbered(appleBookItems(cfg.value(QStringLiteral("novels")).toList()))
                                      : QVariantList{};
    QVariantList movieQueries = cfg.value(QStringLiteral("movieQueries")).toList();
    if (movieQueries.isEmpty()) movieQueries.append(cfg.value(QStringLiteral("name")));
    QVariantList seriesQueries = cfg.value(QStringLiteral("seriesQueries")).toList();
    if (seriesQueries.isEmpty()) seriesQueries.append(cfg.value(QStringLiteral("name")));
    const QVariantList films = enrich ? numbered(cinemetaCanonItems(cfg.value(QStringLiteral("films")).toList(),
        movieQueries, QStringLiteral("movie"))) : QVariantList{};
    const QVariantList shows = enrich ? cinemetaCanonItems(cfg.value(QStringLiteral("shows")).toList(),
        seriesQueries, QStringLiteral("series")) : QVariantList{};
    const QVariantMap firstBook = books.isEmpty() ? QVariantMap{} : books.first().toMap();
    const QVariantMap firstWatch = !films.isEmpty() ? films.first().toMap()
                               : !shows.isEmpty() ? shows.first().toMap() : QVariantMap{};

    QVariantList sections;
    sections.append(heroSection(0, QStringLiteral("saga"), cfg, {},
        QStringLiteral("UNIVERSE  ·  THE SAGA")));
    const QString leftSub = firstBook.isEmpty() ? QStringLiteral("The novels")
        : QStringLiteral("Begin with %1").arg(firstBook.value(QStringLiteral("title")).toString());
    const QString rightSub = firstWatch.isEmpty() ? QStringLiteral("The adaptations")
        : QStringLiteral("Begin with %1").arg(firstWatch.value(QStringLiteral("title")).toString());
    sections.append(dualitySection(sections.size(), QStringLiteral("saga"),
        firstBook, QStringLiteral("Read"), leftSub, firstWatch, QStringLiteral("Watch"), rightSub));
    sections.append(groupSection(QStringLiteral("universe.saga.novels"), sections.size(),
        QStringLiteral("saga"), QStringLiteral("The Novels"), books, QStringLiteral("rail"),
        enrich ? QStringLiteral("%1 books  ·  reading order").arg(books.size()) : QString(), {}, !enrich));
    sections.append(groupSection(QStringLiteral("universe.saga.films"), sections.size(),
        QStringLiteral("saga"), QStringLiteral("The Films"), films, QStringLiteral("rail"),
        QString(), {}, !enrich));
    sections.append(groupSection(QStringLiteral("universe.saga.shows"), sections.size(),
        QStringLiteral("saga"), QStringLiteral("TV Shows"), shows, QStringLiteral("rail"),
        QString(), {}, !enrich));
    const QVariantMap comics = comicDoorItem(cfg);
    if (!comics.isEmpty())
        sections.append(groupSection(QStringLiteral("universe.saga.comics"), sections.size(),
            QStringLiteral("saga"), QStringLiteral("GETCOMICS ARCHIVE"), QVariantList{comics},
            QStringLiteral("portal"), cfg.value(QStringLiteral("comics")).toMap()
                .value(QStringLiteral("line"), QStringLiteral("The canon continues in print.")).toString()));
    return sections;
}


QVariantMap eraGallerySection(int index, const QString &kicker,
                              const QVariantList &columns, const QVariantList &items,
                              const QVariantMap &comic = {})
{
    QVariantMap data{{QStringLiteral("kicker"), kicker},
                     {QStringLiteral("columns"), columns}};
    QVariantList all = items;
    if (!comic.isEmpty()) {
        data.insert(QStringLiteral("comicKey"), comic.value(QStringLiteral("key")));
        all.append(comic);
    }
    return customSection(QStringLiteral("universe.eras"), index, QStringLiteral("ready"),
                         QStringLiteral("universes.eras"), data, all);
}

QVariantList theatreItemsFromSlots(const QVariantList &matched)
{
    QVariantList items;
    for (const QVariant &value : matched) {
        const QVariantMap meta = value.toMap();
        if (!meta.isEmpty()) {
            const QVariantMap item = theatreItem(meta);
            if (!item.isEmpty()) items.append(item);
        }
    }
    return items;
}

// EraUniversePage.qml era gallery/body + SagaApi.js:259-336.
QVariantList buildEras(const QVariantMap &cfg, bool enrich)
{
    const QVariantList movieQueries = cfg.value(QStringLiteral("movieQueries")).toList();
    const QVariantList seriesQueries = cfg.value(QStringLiteral("seriesQueries")).toList();
    const QVariantList moviePool = enrich
        ? pooledCinemeta(QStringLiteral("movie"), movieQueries) : QVariantList{};
    const QVariantList seriesPool = enrich
        ? pooledCinemeta(QStringLiteral("series"), seriesQueries) : QVariantList{};

    QVariantMap firstWatch;
    if (enrich && cfg.contains(QStringLiteral("firstWatch"))) {
        const QVariantList wanted{cfg.value(QStringLiteral("firstWatch"))};
        const QString kind = cfg.value(QStringLiteral("firstWatchKind"),
                                       QStringLiteral("movie")).toString();
        const QVariantList matched = slotByCanon(
            wanted, kind == QLatin1String("series") ? seriesPool : moviePool);
        if (!matched.isEmpty() && !matched.first().toMap().isEmpty())
            firstWatch = theatreItem(matched.first().toMap());
    }

    QVariantList sections;
    const QString kicker = cfg.value(QStringLiteral("eraKicker"),
                                     QStringLiteral("THE ERAS")).toString();
    sections.append(heroSection(
        0, QStringLiteral("era"), cfg,
        firstWatch.isEmpty() ? QVariantList{} : QVariantList{firstWatch},
        QStringLiteral("UNIVERSE  ·  ") + kicker,
        cfg.value(QStringLiteral("firstWatchLabel"),
                  QStringLiteral("Begin here")).toString()));

    QVariantList galleryItems;
    QVariantList columns;
    int columnIndex = 0;
    for (const QVariant &value : cfg.value(QStringLiteral("eras")).toList()) {
        const QVariantMap era = value.toMap();
        QVariantList eraItems;
        if (enrich) {
            const QVariantList matched = slotByCanon(
                era.value(QStringLiteral("titles")).toList(),
                era.value(QStringLiteral("kind")).toString() == QLatin1String("series")
                    ? seriesPool : moviePool);
            eraItems = theatreItemsFromSlots(matched);
        }
        QVariantList keys;
        for (const QVariant &itemValue : eraItems) {
            const QVariantMap item = itemValue.toMap();
            keys.append(item.value(QStringLiteral("key")));
            galleryItems.append(item);
        }
        columns.append(QVariantMap{
            {QStringLiteral("key"), QString::number(columnIndex++)},
            {QStringLiteral("label"), era.value(QStringLiteral("era"))},
            {QStringLiteral("itemKeys"), keys},
            {QStringLiteral("pending"), !enrich}});
    }

    sections.append(eraGallerySection(sections.size(), kicker, columns,
                                      galleryItems, comicDoorItem(cfg)));

    int railIndex = 0;
    for (const QVariant &value : cfg.value(QStringLiteral("rails")).toList()) {
        const QVariantMap rail = value.toMap();
        QVariantList items;
        if (enrich) {
            const QVariantList matched = slotByCanon(
                rail.value(QStringLiteral("titles")).toList(),
                rail.value(QStringLiteral("kind")).toString() == QLatin1String("series")
                    ? seriesPool : moviePool);
            items = theatreItemsFromSlots(matched);
        }
        sections.append(groupSection(
            QStringLiteral("universe.era.rail.%1").arg(railIndex++),
            sections.size(), QStringLiteral("era"),
            rail.value(QStringLiteral("title")).toString(), items,
            QStringLiteral("rail"), QString(), {}, !enrich));
    }

    const bool hasNovels = cfg.contains(QStringLiteral("novels"));
    const QVariantList novels = enrich
        ? numbered(appleBookItems(cfg.value(QStringLiteral("novels")).toList()))
        : QVariantList{};
    if (hasNovels) {
        sections.append(groupSection(
            QStringLiteral("universe.era.books"), sections.size(),
            QStringLiteral("era"),
            cfg.value(QStringLiteral("novelsTitle"),
                      QStringLiteral("The Novels")).toString(),
            novels, QStringLiteral("rail"),
            enrich ? QStringLiteral("%1 books  ·  reading order").arg(novels.size())
                   : QString(), {}, !enrich));
    }
    return sections;
}

// StudioUniversePage.qml:1-5,82-220 + SagaApi.js:337-366.
QVariantList buildStudio(const QVariantMap &cfg, bool enrich)
{
    QVariantList queries = cfg.value(QStringLiteral("movieQueries")).toList();
    if (queries.isEmpty()) queries.append(cfg.value(QStringLiteral("name")));
    const QVariantList pool = enrich
        ? pooledCinemeta(QStringLiteral("movie"), queries) : QVariantList{};
    const QVariantList matched = enrich
        ? slotByCanon(cfg.value(QStringLiteral("filmography")).toList(), pool)
        : QVariantList{};
    const QVariantList films = numbered(theatreItemsFromSlots(matched));

    QVariantMap firstWatch;
    if (enrich && cfg.contains(QStringLiteral("firstWatch"))) {
        const QVariantList first = slotByCanon(
            QVariantList{cfg.value(QStringLiteral("firstWatch"))}, pool);
        if (!first.isEmpty() && !first.first().toMap().isEmpty())
            firstWatch = theatreItem(first.first().toMap());
    }

    QVariantList sections;
    sections.append(heroSection(
        0, QStringLiteral("studio"), cfg,
        firstWatch.isEmpty() ? QVariantList{} : QVariantList{firstWatch},
        QStringLiteral("UNIVERSE  ·  THE STUDIO"),
        cfg.value(QStringLiteral("firstWatchLabel"),
                  QStringLiteral("Begin here")).toString()));
    sections.append(groupSection(
        QStringLiteral("universe.studio.filmography"), sections.size(),
        QStringLiteral("studio"), QStringLiteral("The Filmography"), films,
        QStringLiteral("wall"),
        enrich ? QStringLiteral("%1 features  ·  chronological").arg(films.size())
               : QString(), {}, !enrich));
    return sections;
}

QVariantList genericCinemeta(const QString &kind, const QVariantList &queries)
{
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant &value : queries) {
        const QString query = value.toString();
        if (query.isEmpty()) continue;
        for (const QVariant &metaValue : cinemetaSearch(kind, query)) {
            const QVariantMap meta = metaValue.toMap();
            const QString id = meta.value(QStringLiteral("id")).toString();
            const QString title = meta.value(QStringLiteral("name"),
                meta.value(QStringLiteral("title"))).toString();
            // UniverseApi.js:109-115 — fuzzy Cinemeta hits only enter the
            // generic page when the result title still contains that curated query.
            if (id.isEmpty() || seen.contains(id)
                || !title.contains(query, Qt::CaseInsensitive)) continue;
            seen.insert(id);
            const QVariantMap item = theatreItem(meta);
            if (!item.isEmpty()) out.append(item);
            if (out.size() >= 18) return out;
        }
    }
    return out;
}

QString genericMetaline(const QVariantList &manga, const QVariantList &series,
                        const QVariantList &movies)
{
    QStringList parts;
    if (!manga.isEmpty()) parts.append(QStringLiteral("Manga"));
    if (!series.isEmpty()) {
        parts.append(series.size() == 1
            ? QStringLiteral("1 Series")
            : QStringLiteral("%1 Anime & Series").arg(series.size()));
    }
    if (!movies.isEmpty()) parts.append(QStringLiteral("%1 Films").arg(movies.size()));
    return parts.join(QStringLiteral("   ·   "));
}

// UniverseExtensionPage.qml generic read/watch surface + UniverseApi.js:34-300.
QVariantList buildGeneric(const QVariantMap &cfg, const WorldFeed::Paths &paths, bool enrich)
{
    QVariantList readQueries = cfg.value(QStringLiteral("readQueries")).toList();
    QVariantList seriesQueries = cfg.value(QStringLiteral("seriesQueries")).toList();
    QVariantList movieQueries = cfg.value(QStringLiteral("movieQueries")).toList();
    const QString name = cfg.value(QStringLiteral("name")).toString();
    if (readQueries.isEmpty()) readQueries.append(name);
    if (seriesQueries.isEmpty()) seriesQueries.append(name);
    if (movieQueries.isEmpty()) movieQueries.append(name);

    QVariantList manga;
    QVariantList series;
    QVariantList movies;
    const QString category = cfg.value(QStringLiteral("category")).toString();
    if (enrich) {
        manga = mangaQueryItems(readQueries, paths);
        if (category != QLatin1String("magazine")) {
            series = genericCinemeta(QStringLiteral("series"), seriesQueries);
            movies = genericCinemeta(QStringLiteral("movie"), movieQueries);
        }
    }

    QVariantMap firstRead = manga.isEmpty() ? QVariantMap{} : manga.first().toMap();
    QVariantMap firstWatch = !series.isEmpty() ? series.first().toMap()
                            : !movies.isEmpty() ? movies.first().toMap() : QVariantMap{};

    QVariantMap hero = heroSection(0, QStringLiteral("generic"), cfg, {},
        category == QLatin1String("magazine")
            ? QStringLiteral("UNIVERSE  ·  THE MAGAZINE")
            : QStringLiteral("UNIVERSE"));
    QVariantMap heroData = hero.value(QStringLiteral("data")).toMap();
    heroData.insert(QStringLiteral("metaline"), genericMetaline(manga, series, movies));
    hero.insert(QStringLiteral("data"), heroData);
    QVariantList sections{hero};

    if (category != QLatin1String("magazine")) {
        const QString readSub = firstRead.isEmpty() ? QStringLiteral("Start the manga")
            : firstRead.value(QStringLiteral("title")).toString();
        const QString watchSub = firstWatch.isEmpty() ? QStringLiteral("Start watching")
            : firstWatch.value(QStringLiteral("title")).toString();
        sections.append(dualitySection(sections.size(), QStringLiteral("generic"),
            firstRead, QStringLiteral("Read"), readSub,
            firstWatch, QStringLiteral("Watch"), watchSub));
    }
    sections.append(groupSection(QStringLiteral("universe.generic.manga"), sections.size(),
        QStringLiteral("generic"), category == QLatin1String("magazine")
            ? QStringLiteral("Current Lineup") : QStringLiteral("Manga"),
        manga, QStringLiteral("rail"), QString(), {}, !enrich));
    if (category != QLatin1String("magazine")) {
        sections.append(groupSection(QStringLiteral("universe.generic.series"), sections.size(),
            QStringLiteral("generic"), QStringLiteral("Anime & Series"),
            series, QStringLiteral("rail"), QString(), {}, !enrich));
        sections.append(groupSection(QStringLiteral("universe.generic.films"), sections.size(),
            QStringLiteral("generic"), QStringLiteral("Films"),
            movies, QStringLiteral("rail"), QString(), {}, !enrich));
    }
    return sections;
}

QVariantList buildFor(const FeedContext &context, bool enrich)
{
    const QString extensionId = context.params.value(QStringLiteral("extensionId")).toString();
    QString name = context.params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) name = context.params.value(QStringLiteral("title")).toString().trimmed();

    if (extensionId == QLatin1String(kDcau)) return buildDcau(context);
    if (extensionId == QLatin1String(kStarWars)) return buildStarWars(context, enrich);
    if (extensionId == QLatin1String(kCosmere) && name.isEmpty()) name = QStringLiteral("Cosmere");

    const QVariantMap cfg = configFor(name);
    if (cfg.isEmpty()) {
        QVariantMap error = customSection(QStringLiteral("universe.error"), 0,
            QStringLiteral("error"), {});
        // UniverseExtensionPage.qml:168-180 exact empty/failure copy.
        error.insert(QStringLiteral("error"),
                     QStringLiteral("This universe isn't installed yet."));
        return {error};
    }

    const QString category = cfg.value(QStringLiteral("category")).toString();
    if (extensionId == QLatin1String(kCosmere) || category == QLatin1String("cosmere"))
        return buildCosmere(cfg, enrich);
    if (category == QLatin1String("saga")) return buildSaga(cfg, enrich);
    if (category == QLatin1String("eras")) return buildEras(cfg, enrich);
    if (category == QLatin1String("studio")) return buildStudio(cfg, enrich);
    return buildGeneric(cfg, context.paths, enrich);
}

QVariantList build(const FeedContext &context)
{
    return buildFor(context, false);
}

QVariantList enrich(const FeedContext &context)
{
    if (context.params.value(QStringLiteral("extensionId")).toString() == QLatin1String(kDcau))
        return context.baseSections;
    return buildFor(context, true);
}

QMetaObject::Connection watchExtensions(QObject *object, QObject *receiver,
                                        std::function<void()> changed)
{
    auto *extensions = qobject_cast<ExtensionsStore *>(object);
    if (!extensions) return {};
    return QObject::connect(extensions, &ExtensionsStore::changed, receiver,
                            [changed = std::move(changed)] { changed(); });
}

const bool registered = [] {
    FeedRegistry::Entry entry;
    entry.name = QStringLiteral("detail.universe");
    entry.valid = valid;
    entry.initial = initial;
    entry.build = build;
    entry.needsProgress = true;
    entry.enrich = enrich;
    entry.capture = capture;
    entry.ownerSignals.append({QStringLiteral("Extensions"), watchExtensions});
    return FeedRegistry::add(std::move(entry));
}();

} // namespace

