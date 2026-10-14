import os
import sqlite3
import unittest
from pathlib import Path

from contracts import build_contracts, imdb_query, mal_query, comics_search_query, curated_query, max_published_year, original_sources


class Semantics(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(':memory:')
        self.addCleanup(self.db.close)
        self.db.create_function('max_published_year',1,max_published_year,deterministic=True)
        self.db.executescript("""
        ATTACH ':memory:' AS imdb; ATTACH ':memory:' AS mal; ATTACH ':memory:' AS comics;
        CREATE TABLE imdb.title(tt TEXT,type TEXT,title TEXT,year INTEGER,endYear INTEGER,runtimeMin INTEGER,genres TEXT,rating REAL,votes INTEGER,episodes INTEGER,origLang TEXT,isAnime INTEGER,norm_title TEXT);
        CREATE TABLE imdb.genre(tt TEXT,genre TEXT);
        INSERT INTO imdb.title VALUES('tt1','movie','Star',2020,0,90,'[]',8,100,0,'en',0,'star'),('tt2','mini','Star Two',2021,0,40,'[]',9,200,8,'ja',1,'star two'),('tt3','series','Other Star',0,0,0,'[]',NULL,100,0,'',0,'other star');
        INSERT INTO imdb.genre VALUES('tt1','Drama'),('tt2','Animation'),('tt3','Drama');
        CREATE TABLE mal.manga(mal_id INTEGER,title TEXT,title_english TEXT,type TEXT,score REAL,scored_by INTEGER,members INTEGER,favorites INTEGER,year INTEGER,start_date TEXT,cover TEXT,tags TEXT,explicit INTEGER,norm_title TEXT);
        CREATE TABLE mal.classification(medium TEXT,axis TEXT,value TEXT,mal_id INTEGER);
        INSERT INTO mal.manga VALUES(1,'A','','Manga',9.9,100,100,1,2020,'2020-01-01','','[]',0,'a'),(2,'B','','Manga',8.9,10000,100,1,2020,'2020-01-01','','[]',0,'b'),(3,'C','','Manga',NULL,0,100,1,0,'bad','','[]',0,'c'),(4,'Adult','','Manga',10,100000,200,1,2100,'2100-01-01','','[]',1,'adult');
        INSERT INTO mal.classification VALUES('manga','genre','Romance',1),('manga','demographic','Seinen',1),('manga','genre','Romance',2),('manga','demographic','Seinen',2),('anime','genre','Romance',3),('manga','genre','Romance',4);
        CREATE TABLE comics.series(gcd_id INTEGER,title TEXT,year INTEGER,year_ended INTEGER,publisher TEXT,cover TEXT);
        CREATE TABLE comics.series_stats(series_id INTEGER,downloads INTEGER);
        INSERT INTO comics.series VALUES(1,'Alpha Beta',2000,0,'P',''),(2,'Alpha Beta Extra',2001,0,'P',''),(3,'Beta Alpha',2002,0,'P',''),(4,'Alpha 100%_Beta',2003,0,'P','');
        INSERT INTO comics.series_stats VALUES(1,1),(2,20),(3,100),(4,200);
        CREATE TABLE comics.curated_series(locg_id TEXT,rank INTEGER,title TEXT,norm_title TEXT,year INTEGER,publisher TEXT,cover TEXT,synopsis TEXT);
        CREATE TABLE comics.curated_genre(locg_id TEXT,genre TEXT);
        CREATE TABLE comics.curated_edition(locg_id TEXT,format TEXT,published TEXT,available INTEGER,getcomics_post TEXT);
        CREATE TABLE comics.curated_series_coverage(locg_id TEXT,availability_state TEXT,coverage_ratio REAL,source_count INTEGER,official_source_count INTEGER,community_source_count INTEGER,latest_source_date TEXT);
        INSERT INTO comics.curated_series VALUES('L1',NULL,'A','a',2000,'P','',''),('L2',2,'B','b',2000,'P','','');
        INSERT INTO comics.curated_genre VALUES('L1','Adventure'),('L2','Adventure');
        INSERT INTO comics.curated_edition VALUES('L1',' Hardcover ','1999-2001',1,'post'),('L1','Paperback','future 2101',1,''),('L2','Paperback','2000',0,'');
        INSERT INTO comics.curated_series_coverage VALUES('L1','partial',0.5,2,0,2,'2020-01-01'),('L2','complete',1,2,2,0,'2021-01-01');
        """)

    def rows(self, query):
        return self.db.execute(query['sql'], query['args']).fetchall()

    def test_imdb_filters_exclusions_and_native_prefix(self):
        q = imdb_query({'type':'movie','genre':'Drama','ratingMin':7,'votesMin':50,'votesMax':100,'yearFrom':2000,'yearTo':2020,'runtimeMax':100,'lang':'en','notGenre':['Animation'],'excludeAnime':True})
        self.assertEqual([r[0] for r in self.rows(q)], ['tt1'])
        self.assertEqual([r[0] for r in self.rows(imdb_query({}, prefix='star'))], ['tt1','tt2'])
        self.assertEqual(self.rows(imdb_query({'notLang':'en','yearTo':2021})), [('tt2','mini','Star Two',2021,0,40,'[]',9.0,200,8,'ja',1)])
        with self.assertRaises(ValueError): imdb_query({'unknown':1})

    def test_mal_mean_is_selected_population_and_medium_qualified(self):
        filters=[('genre','romance'),('demographic','SEINEN')]
        self.db.execute("INSERT INTO mal.manga VALUES(5,'Low','','Manga',7,100,1,0,0,'bad','','[]',0,'low')")
        self.db.executemany('INSERT INTO mal.classification VALUES(?,?,?,?)',[('manga','genre','Romance',5),('manga','demographic','Seinen',5)])
        query=mal_query('top-rated',filters,roots=[1,2,5])
        rows=self.rows(query)
        self.assertEqual([r[0] for r in rows],[2,1,5])
        self.assertEqual(self.rows(mal_query('mean',filters,roots=[1,2,5])),[(8.6,)])
        self.assertEqual(self.rows(mal_query('mean',filters,roots=[1])),[(9.9,)])
        self.assertEqual([r[0] for r in self.rows(mal_query('popular',[('genre','romance')]))],[1,2,5])

    def test_mal_nulls_ties_dates_and_explicit(self):
        self.assertEqual([r[0] for r in self.rows(mal_query('popular'))],[1,2,3])
        self.assertEqual([r[0] for r in self.rows(mal_query('new-releases',as_of='2026-10-14'))],[2,1])
        self.assertEqual([r[0] for r in self.rows(mal_query('popular',include_explicit=True))],[4,1,2,3])
        self.assertIsNone(self.rows(mal_query('popular'))[-1][4])
        self.assertEqual(self.rows(mal_query('top-rated',roots=[])),[])
        with self.assertRaises(ValueError): mal_query('popular',[('publisher','P')])

    def test_comics_phrase_tiers_all_words_and_literal_wildcards(self):
        self.assertEqual([r[0] for r in self.rows(comics_search_query('Alpha Beta'))],[1,2,4,3])
        self.assertEqual([r[0] for r in self.rows(comics_search_query('Alpha 100%_Beta'))],[4])
        self.assertEqual(self.rows(comics_search_query('%%%')),[])

    def test_curated_and_facets_preserve_nullable_rank_coverage_and_aggregates(self):
        rows=self.rows(curated_query([('genre','adventure'),('publisher','p'),('format','hardcover')]))
        self.assertEqual(len(rows),1)
        names=curated_query([])['columns'];facts=dict(zip(names,rows[0]))
        self.assertEqual(facts['locg_id'],'L1');self.assertIsNone(facts['rank'])
        self.assertEqual((facts['ed_count'],facts['avail_count']), (2,1))
        self.assertEqual((facts['avail_frac'],facts['max_year']),(.5,2001))
        self.assertEqual((facts['official_source_count'],facts['community_source_count']),(0,2))
        self.assertEqual([r[0] for r in self.rows(curated_query([('availability','available')]))],['L2'])
        self.assertEqual([r[0] for r in self.rows(curated_query([],catalog='community-collections'))],['L1'])

    def test_projection_transformations_preserve_selected_queries_and_multiplicity(self):
        self.db.executescript("""
        CREATE TABLE proj_imdb_title AS SELECT * FROM imdb.title;
        CREATE TABLE proj_imdb_genre AS SELECT * FROM imdb.genre;
        CREATE TABLE proj_mal_manga AS SELECT * FROM mal.manga;
        CREATE TABLE proj_mal_classification AS SELECT * FROM mal.classification;
        CREATE TABLE proj_comics_series AS SELECT s.*,coalesce(t.downloads,0) downloads FROM comics.series s LEFT JOIN comics.series_stats t ON t.series_id=s.gcd_id;
        """)
        self.db.execute("INSERT INTO mal.classification VALUES('manga','genre','Romance',1)")
        self.db.execute("INSERT INTO proj_mal_classification VALUES('manga','genre','Romance',1)")
        for factory,kwargs in [(imdb_query,{'query':{'genre':'Drama','order':'rating'},'roots':['tt1','tt3']}),(mal_query,{'catalog':'top-rated','filters':[('genre','romance')],'roots':[1,2]}),(comics_search_query,{'text':'Alpha Beta','roots':[1,2,4]})]:
            self.assertEqual(self.rows(factory(**kwargs)),self.rows(factory(**kwargs,projection=True)))
        # The first native classification join intentionally retains multiplicity;
        # a projection dedupe cannot silently change this population or its mean.
        self.assertEqual([r[0] for r in self.rows(mal_query('popular',[('genre','romance')],roots=[1,2]))],[1,1,2])

    def test_curated_projection_inputs_and_dates_match_original(self):
        source=curated_query([])
        columns=source['columns']
        self.db.execute('CREATE TABLE proj_comics_curated ('+','.join(columns)+')')
        self.db.executemany('INSERT INTO proj_comics_curated VALUES('+','.join('?' for _ in columns)+')',self.rows(source))
        self.db.executescript('CREATE TABLE proj_comics_curated_genre AS SELECT * FROM comics.curated_genre; CREATE TABLE proj_comics_curated_format AS SELECT DISTINCT locg_id,format FROM comics.curated_edition;')
        facets=[('publisher','p'),('genre','adventure'),('format','hardcover')]
        self.assertEqual(self.rows(curated_query(facets)),self.rows(curated_query(facets,projection=True)))
        self.assertEqual(max_published_year('1899 1999-2001 2101'),2001)
        self.assertEqual(max_published_year('x20002'),2000)  # native regex has no boundaries
        self.assertEqual(max_published_year('undated'),0)


@unittest.skipUnless(os.environ.get('COLOSSEUM_CONTRACT_SOURCE_DIR'), 'set source directory for immutable real-source contract checks')
class OriginalSources(unittest.TestCase):
    def test_original_snapshot_hashes_oracles_and_repeatability(self):
        source=Path(os.environ['COLOSSEUM_CONTRACT_SOURCE_DIR'])
        a=build_contracts(source,as_of='2026-10-14')
        b=build_contracts(source,as_of='2026-10-14')
        self.assertEqual(a,b)
        self.assertGreaterEqual(len(a['cases']),12)
        self.assertTrue(all(c['expectedDigest'] and c['sourceSql'] and c['projectionSql'] for c in a['cases']))
        self.assertEqual(len(a['sourceHashes']),4)

    def test_real_sources_open_query_only(self):
        with original_sources(os.environ['COLOSSEUM_CONTRACT_SOURCE_DIR']) as (db,hashes):
            self.assertEqual(db.execute('PRAGMA query_only').fetchone(),(1,))
            self.assertEqual(len(db.execute('PRAGMA database_list').fetchall()),5)
            with self.assertRaises(sqlite3.OperationalError):
                db.execute('CREATE TEMP TABLE prohibited_contract_write(x)')


if __name__ == '__main__': unittest.main()
