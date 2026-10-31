CREATE TABLE manga (mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT, type TEXT,
 score REAL, scored_by INTEGER, members INTEGER, status TEXT, volumes INTEGER, chapters INTEGER,
 year INTEGER, cover TEXT, synopsis TEXT, credits TEXT, tags TEXT, explicit INTEGER NOT NULL DEFAULT 0,
 start_date TEXT NOT NULL DEFAULT '', favorites INTEGER NOT NULL DEFAULT 0);
CREATE TABLE tag (medium TEXT, tag TEXT, mal_id INTEGER);
CREATE TABLE tag_count (medium TEXT, tag TEXT, total INTEGER, PRIMARY KEY(medium,tag));
CREATE TABLE classification (medium TEXT, axis TEXT, value TEXT, mal_id INTEGER);
CREATE TABLE classification_count (medium TEXT, axis TEXT, value TEXT, total INTEGER, PRIMARY KEY(medium,axis,value));
INSERT INTO manga (mal_id,title,type,score,scored_by,members,start_date,explicit,tags) VALUES
 (1,'Berserk','Manga',9.4,300000,600000,'1989-08-25',0,'[]'),
 (2,'Solo Leveling','Manhwa',8.7,150000,500000,'2018-03-04',0,'[]'),
 (3,'Manhua fixture','Manhua',8.0,5000,300000,'2019-01-01',0,'[]'),
 (4,'Prose fixture','Novel',9.0,5000,250000,'2020-01-01',0,'[]'),
 (5,'Adult Manhwa','Manhwa',9.5,90000,700000,'2025-01-01',1,'[]'),
 (6,'Low-vote Manhwa','mAnHwA',9.9,1,40000,'2024-01-01',0,'[]'),
 (7,'Future Manhwa','Manhwa',7.0,500,50,'2099-01-01',0,'[]'),
 (8,'Invalid-date Manhwa','Manhwa',7.0,500,40,'2021-00-00',0,'[]');
WITH RECURSIVE n(x) AS (SELECT 0 UNION ALL SELECT x+1 FROM n WHERE x<29)
 INSERT INTO manga (mal_id,title,type,score,scored_by,members,start_date,tags)
 SELECT 100+x,'Manhwa '||x,'Manhwa',6.0,1000,10000-x,'2001-01-01','[]' FROM n;
INSERT INTO classification VALUES ('manga','genre','Action',2),('manga','genre','Action',5),
 ('manga','genre','Action',6),('manga','demographic','Seinen',2),('manga','demographic','Seinen',5);
INSERT INTO classification_count SELECT medium,axis,value,COUNT(*) FROM classification GROUP BY medium,axis,value;
