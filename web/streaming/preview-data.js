// Deliberately illustrative. These titles and progress are not account or availability claims.
(() => {
const title = (id, providerId, type, name, art, genres = []) => ({ id, providerId, type, title: name, art, genres, description: 'Explore this title in the provider catalogue.' });

const previewData = {
  accountInitial: 'H',
  resumes: [
    { id: 'bes', providerId: 'nfx', type: 'series', title: 'Blue Eye Samurai', episode: 'S1 E5 · The Tale of the Ronin and the Bride', progress: 0.63, remaining: '18 min left', art: 'ink', description: 'A journey across a world drawn in steel and winter.', episodes: [{ id: 'bes-s1e5', season: 1, label: 'Episode 5 · Continue', progress: .63 }, { id: 'bes-s1e6', season: 1, label: 'Episode 6 · Up next' }] },
    { id: 'fallout', providerId: 'amp', type: 'series', title: 'Fallout', episode: 'S1 E3 · The Head', progress: 0.32, remaining: '41 min left', art: 'ember', description: 'The world outside the vault has other plans.' },
    { id: 'severance', providerId: 'atp', type: 'series', title: 'Severance', episode: 'S1 E6 · Hide and Seek', progress: 0.79, remaining: '9 min left', art: 'corridor', description: 'A line between two lives begins to disappear.', episodes: [{ id: 'sev-s1e6', season: 1, label: 'Episode 6 · Continue', progress: .79 }, { id: 'sev-s1e7', season: 1, label: 'Episode 7 · Up next' }, { id: 'sev-s2e1', season: 2, label: 'Season 2 · Episode 1' }] }
  ],
  catalogs: {
    nfx: [
      { id: 'series', name: 'Stories worth staying in', titles: [title('bes','nfx','series','Blue Eye Samurai','ink',['Animation','Drama']),title('arcane','nfx','series','Arcane','violet',['Animation','Fantasy']),title('crown','nfx','series','The Crown','marble',['Drama']),title('onepiece','nfx','series','One Piece','ocean',['Adventure']),title('dark','nfx','series','Dark','forest',['Mystery']),title('beef','nfx','series','Beef','ember',['Comedy','Drama'])] },
      { id: 'movies', name: 'A film for tonight', titles: [title('glass-onion','nfx','movie','Glass Onion','citrus',['Mystery']),title('roma','nfx','movie','Roma','marble',['Drama']),title('irishman','nfx','movie','The Irishman','shadow',['Crime']),title('pinocchio','nfx','movie','Guillermo del Toro’s Pinocchio','forest',['Animation']),title('all-quiet','nfx','movie','All Quiet on the Western Front','smoke',['Drama'])] }
    ],
    amp: [
      { id: 'series', name: 'Series to discover', titles: [title('fallout','amp','series','Fallout','ember',['Drama']),title('boys','amp','series','The Boys','shadow',['Action']),title('fleabag','amp','series','Fleabag','marble',['Comedy']),title('rings','amp','series','The Rings of Power','golden',['Fantasy']),title('reacher','amp','series','Reacher','smoke',['Action'])] },
      { id: 'movies', name: 'Films to discover', titles: [title('sound-metal','amp','movie','Sound of Metal','ink',['Drama']),title('saltburn','amp','movie','Saltburn','violet',['Drama']),title('air','amp','movie','Air','citrus',['Drama'])] }
    ],
    hbm: [
      { id: 'series', name: 'Series to discover', titles: [title('last-of-us','hbm','series','The Last of Us','forest',['Drama']),title('succession','hbm','series','Succession','marble',['Drama']),title('white-lotus','hbm','series','The White Lotus','citrus',['Drama']),title('house-dragon','hbm','series','House of the Dragon','ember',['Fantasy'])] },
      { id: 'movies', name: 'Films to discover', titles: [title('dune','hbm','movie','Dune','golden',['Science fiction']),title('batman','hbm','movie','The Batman','shadow',['Action']),title('barbie','hbm','movie','Barbie','violet',['Comedy'])] }
    ],
    dnp: [
      { id: 'series', name: 'Series to discover', titles: [title('bear','dnp','series','The Bear','ember',['Drama']),title('shogun','dnp','series','Shōgun','smoke',['Drama']),title('andor','dnp','series','Andor','ocean',['Science fiction']),title('loki','dnp','series','Loki','golden',['Fantasy'])] },
      { id: 'movies', name: 'Films to discover', titles: [title('soul','dnp','movie','Soul','violet',['Animation']),title('encanto','dnp','movie','Encanto','citrus',['Animation']),title('coco','dnp','movie','Coco','marble',['Animation'])] }
    ],
    atp: [
      { id: 'series', name: 'Series to discover', titles: [title('severance','atp','series','Severance','corridor',['Drama']),title('foundation','atp','series','Foundation','ocean',['Science fiction']),title('slow-horses','atp','series','Slow Horses','shadow',['Thriller']),title('ted-lasso','atp','series','Ted Lasso','golden',['Comedy'])] },
      { id: 'movies', name: 'Films to discover', titles: [title('killers','atp','movie','Killers of the Flower Moon','ember',['Drama']),title('finch','atp','movie','Finch','smoke',['Science fiction']),title('tetris','atp','movie','Tetris','violet',['Drama'])] }
    ]
  }
};

window.StreamingPreviewData = previewData;
})();
