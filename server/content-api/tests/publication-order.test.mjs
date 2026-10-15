import test from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {publicationSql} from '../projection/publication_sql.mjs';
test('MAL score ties follow packed source ordinal despite an eligible helper index',()=>{
 const db=new DatabaseSync(':memory:');
 try{
  db.exec('CREATE TABLE den_mal_anime(_ordinal INTEGER PRIMARY KEY,mal_id INT,score REAL,members INT,scored_by INT);INSERT INTO den_mal_anime VALUES(1,21899,8.43,42884,16322),(2,24687,8.43,97577,47815);CREATE INDEX helper ON den_mal_anime(score DESC,members DESC)');
  const sql='SELECT mal_id FROM den_mal_anime r WHERE r.members>=20000 AND r.members<=150000 AND r.scored_by>=2000 ORDER BY r.score DESC LIMIT 20';
  assert.deepEqual(db.prepare(sql).all().map(x=>x.mal_id),[24687,21899],'negative control reproduces helper-index reordering');
  assert.deepEqual(db.prepare(publicationSql(sql)).all().map(x=>x.mal_id),[21899,24687]);
 }finally{db.close();}
});
test('count tie protection is retained and unrelated queries are unchanged',()=>{
 assert.equal(publicationSql('SELECT * FROM lib_mal_tag_count WHERE medium=?'),'SELECT * FROM lib_mal_tag_count NOT INDEXED WHERE medium=?');
 assert.equal(publicationSql('SELECT * FROM den_comics_series'),'SELECT * FROM den_comics_series');
});
