"""Prepare identical bounded catalogue fixtures; original SQLite connections are read-only."""
import argparse
from contextlib import ExitStack, closing
import hashlib
import json
import math
from pathlib import Path
import re
import sqlite3
import time

ROOT = Path(__file__).resolve().parents[1]
DEFINITIONS = {
    "b_imdb_title": ("imdb", "title", "tt", ["tt", "title", "norm_title", "year", "rating", "votes"]),
    "b_imdb_genre": ("imdb", "genre", "tt", ["tt", "genre"]),
    "b_mal_manga": ("mal", "manga", "mal_id", ["mal_id", "title", "norm_title", "members", "score", "scored_by", "explicit", "start_date"]),
    "b_mal_anime": ("mal", "anime", "mal_id", ["mal_id", "title", "norm_title", "members", "score"]),
    "b_mal_classification": ("mal", "classification", "mal_id", ["medium", "axis", "value", "mal_id"]),
    "b_comics_series": ("comics", "series", "gcd_id", ["gcd_id", "title", "year"]),
    "b_comics_stats": ("comics", "series_stats", "series_id", ["series_id", "downloads"]),
    "b_comics_download": ("comics", "download", "series_id", ["post_id", "series_id", "title", "date"]),
    "b_comics_mirror": ("comics", "download_mirror", "post_id", ["post_id", "url", "host", "label"]),
    "b_tankoban_series": ("tankoban", "series", "mal_id", ["mal_id", "volume_count", "count_basis"]),
    "b_tankoban_volumes": ("tankoban", "volumes", "mal_id", ["mal_id", "number", "cover_url", "name"]),
}
INDEXES = {
    "b_imdb_title": [("tt",), ("norm_title",)], "b_imdb_genre": [("tt", "genre")],
    "b_mal_manga": [("mal_id",), ("norm_title",)], "b_mal_anime": [("mal_id",), ("norm_title",)],
    "b_mal_classification": [("medium", "mal_id", "axis", "value"), ("medium", "axis", "value", "mal_id")],
    "b_comics_series": [("gcd_id",), ("title",)], "b_comics_stats": [("series_id",)],
    "b_comics_download": [("series_id", "post_id"), ("post_id",)], "b_comics_mirror": [("post_id",)],
    "b_tankoban_series": [("mal_id",)], "b_tankoban_volumes": [("mal_id", "number")],
}


def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)


def canonical_rows(rows):
    # JS JSON.stringify emits integral numeric values without '.0'. All result fields are scalars.
    return [[int(v) if isinstance(v, float) and v.is_integer() else v for v in row] for row in rows]


def digest(rows):
    return hashlib.sha256(compact(canonical_rows(rows)).encode("utf-8")).hexdigest()


def quote(name):
    if not re.fullmatch(r"[a-zA-Z_][a-zA-Z_0-9]*", name):
        raise ValueError("Unsafe identifier")
    return '"' + name + '"'


def file_hash(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def sample(values, count):
    take = min(count,len(values))
    return [values[math.floor(j * len(values)/take)] for j in range(take)] if take else []


def choose(db):
    imdb = sample([r[0] for r in db.execute("SELECT tt FROM imdb.title ORDER BY tt")], 64)
    imdb += [r[0] for r in db.execute("SELECT tt FROM imdb.title WHERE norm_title LIKE 'star%' ORDER BY tt LIMIT 8")]
    imdb += [r[0] for r in db.execute("SELECT tt FROM imdb.title WHERE title GLOB '*[^ -~]*' ORDER BY tt LIMIT 4")]
    manga = sample([r[0] for r in db.execute("SELECT mal_id FROM mal.manga ORDER BY mal_id")], 48)
    anime = sample([r[0] for r in db.execute("SELECT mal_id FROM mal.anime ORDER BY mal_id")], 32)
    comic = [r[0] for r in db.execute("SELECT s.gcd_id FROM comics.series s JOIN comics.series_stats t ON t.series_id=s.gcd_id WHERE t.downloads BETWEEN 1 AND 3 AND instr(trim(s.title),' ')>0 ORDER BY s.gcd_id LIMIT 8")]
    tanko = [r[0] for r in db.execute("SELECT mal_id FROM tankoban.series WHERE volume_count BETWEEN 2 AND 12 ORDER BY mal_id LIMIT 12")]
    manga += tanko
    return {"imdb": sorted(set(imdb)), "manga": sorted(set(manga)), "anime": sorted(set(anime)), "comics": comic, "tankoban": tanko}


def build_source_ctes(tables):
    definitions = []
    for table in tables:
        name = table["name"]
        dataset, source, key, columns = DEFINITIONS[name]
        columns_sql = ",".join(quote(c) for c in columns)
        selected = table["selectedKeys"]
        literal = ",".join("'" + str(v).replace("'", "''") + "'" for v in selected) or "NULL"
        source_columns = columns_sql + (",rowid AS source_rowid" if name == "b_comics_mirror" else "")
        definitions.append(f'{name} AS (SELECT {source_columns} FROM {dataset}.{quote(source)} WHERE {quote(key)} IN ({literal})' + (" AND medium IN ('anime','manga')" if name == "b_mal_classification" else "") + ")")
    return ",".join(definitions)


def source_sql(sql, ctes):
    if sql.startswith("WITH RECURSIVE "):
        return "WITH RECURSIVE " + ctes + "," + sql[len("WITH RECURSIVE "):]
    if sql.startswith("WITH "):
        return "WITH " + ctes + "," + sql[len("WITH "):]
    return "WITH " + ctes + " " + sql


def boundary_cases():
    result = []
    for alphabet in ("ascii", "unicode"):
        for size in (49, 50, 51):
            pattern = "z" * (size-1) + "%" if alphabet == "ascii" else "é" * ((size-1)//2) + ("z" if (size-1)%2 else "") + "%"
            result.append({"id": f"like_{alphabet}_{size}", "sql": "SELECT tt,title FROM b_imdb_title WHERE norm_title LIKE ? ORDER BY tt LIMIT 25", "args": [pattern], "patternBytes": len(pattern.encode("utf-8")), "coverage": "Real-table LIKE boundary; remote rejection is a compatibility failure, not a parity pass"})
    return result


def prepare(source_dir, output_dir, max_writes=9000):
    manifest = json.loads((ROOT / "source-manifest.json").read_text("utf-8"))
    hashes = {dataset: file_hash(source_dir / (dataset + "_catalog.db")) for dataset in ("imdb", "mal", "comics", "tankoban")}
    for dataset, value in hashes.items():
        if value != manifest["sources"][dataset]["sha256"]:
            raise ValueError("Source snapshot mismatch: " + dataset)
    with ExitStack() as stack:
        source = stack.enter_context(closing(sqlite3.connect(":memory:",uri=True)))
        for dataset in hashes:
            uri = (source_dir / (dataset + "_catalog.db")).resolve().as_uri() + "?mode=ro"
            source.execute(f"ATTACH DATABASE ? AS {dataset}", (uri,))
        source.execute("PRAGMA query_only=ON")
        selections = choose(source)
        if any(not values for values in selections.values()):
            raise ValueError("Required sample population unavailable")
        tables, statements = [], []
        projected = stack.enter_context(closing(sqlite3.connect(":memory:")))
        for name, (dataset, source_name, key, columns) in DEFINITIONS.items():
            keys = selections["manga"] if name == "b_mal_manga" else selections["anime"] if name == "b_mal_anime" else sorted(set(selections["manga"] + selections["anime"])) if name == "b_mal_classification" else selections[dataset]
            if name == "b_comics_mirror":
                keys = [r[0] for r in source.execute("SELECT post_id FROM comics.download WHERE series_id IN (" + ",".join("?" for _ in selections["comics"]) + ") ORDER BY post_id", selections["comics"])]
            info = {r[1]: r[2] for r in source.execute(f'PRAGMA {dataset}.table_info({quote(source_name)})')}
            output_columns = columns + (["source_rowid"] if name == "b_comics_mirror" else [])
            declaration = ",".join(quote(c) + " " + (info[c] or "TEXT") for c in columns)
            if name == "b_comics_mirror":
                declaration += ',"source_rowid" INTEGER'
            ddl = f"CREATE TABLE {name} ({declaration})"
            statements.append(ddl)
            projected.execute(ddl)
            cols = ",".join(quote(c) for c in columns) + (",rowid" if name == "b_comics_mirror" else "")
            rows = [list(r) for r in source.execute(f'SELECT {cols} FROM {dataset}.{quote(source_name)} WHERE {quote(key)} IN (' + ",".join("?" for _ in keys) + ") ORDER BY rowid", keys)]
            projected.executemany(f"INSERT INTO {name} VALUES(" + ",".join("?" for _ in output_columns) + ")", rows)
            for i, index_columns in enumerate(INDEXES[name]):
                index = f"CREATE INDEX {name}_idx{i} ON {name}(" + ",".join(quote(c) for c in index_columns) + ")"
                projected.execute(index)
                statements.append(index)
            tables.append({"name": name, "columns": output_columns, "rows": rows, "rowCount": len(rows), "indexCount": len(INDEXES[name]), "selectedKeys": keys, "estimatedWrites": len(rows) * (1 + len(INDEXES[name])) + math.ceil(len(rows)/32)*10})
        estimated = sum(table["estimatedWrites"] for table in tables)
        if estimated > max_writes or max_writes >= 10000:
            raise ValueError(f"Seed budget exceeds bounded allowance: {estimated}/{max_writes}")
        root = tables[0]["rows"]
        exact = next(r[2] for r in root if r[2])
        unicode_norm = next((r[1] for r in root if r[1] and not r[1].isascii()), None)
        if unicode_norm is None:
            raise ValueError("No real Unicode display title sampled")
        manga_rows = next(t["rows"] for t in tables if t["name"] == "b_mal_manga")
        classes = next(t["rows"] for t in tables if t["name"] == "b_mal_classification")
        candidates = [r[0] for r in manga_rows if r[6] == 0]
        facets = None
        for mal_id in candidates:
            genre = next((r[2] for r in classes if r[0] == "manga" and r[1] == "genre" and r[3] == mal_id), None)
            demo = next((r[2] for r in classes if r[0] == "manga" and r[1] == "demographic" and r[3] == mal_id), None)
            if genre and demo:
                facets = ["genre", genre.lower(), "demographic", demo.lower()]
                break
        if facets is None:
            raise ValueError("No non-explicit manga with both facets")
        title = next(t["rows"][0][1] for t in tables if t["name"] == "b_comics_series")
        words = title.split()
        escape = lambda s: s.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
        anime_tag = next(r[2] for r in classes if r[0] == "anime" and r[3] in selections["anime"])
        tanko = selections["tankoban"][0]
        bindings = {"imdb_exact": [exact], "imdb_prefix": [exact[:4]+"%", exact[:4]], "imdb_substring": ["%"+exact[:4]+"%"], "imdb_unicode": [unicode_norm[:4]+"%"], "mal_facets": facets, "mal_one_facet": facets[:2], "anime_tag": [anime_tag], "comics_words": ["%"+escape(words[0])+"%", "%"+escape(words[1])+"%", title, escape(title)+"%"], "comic_id": [selections["comics"][0]], "tankoban_id": [tanko], "tankoban_overlay": [tanko,tanko,tanko]}
        bindings["imdb_genre"] = [next(t["rows"][0][1] for t in tables if t["name"] == "b_imdb_genre")]
        ctes = build_source_ctes(tables)
        queries = json.loads((Path(__file__).parent / "queries.json").read_text("utf-8")) + boundary_cases()
        for query in queries:
            query["args"] = bindings[query["bind"]] if "bind" in query else query["args"]
            query["sourceSql"] = source_sql(query["sql"], ctes)
            before = time.perf_counter()
            expected = canonical_rows(source.execute(query["sourceSql"], query["args"]).fetchall())
            query["sourceElapsedMs"] = (time.perf_counter()-before)*1000
            before = time.perf_counter()
            actual = canonical_rows(projected.execute(query["sql"], query["args"]).fetchall())
            query["projectionElapsedMs"] = (time.perf_counter()-before)*1000
            if expected != actual:
                raise ValueError("Source/projection result mismatch: " + query["id"])
            query["expectedRows"] = expected
            query["expectedColumns"] = [column[0] for column in projected.execute(query["sql"], query["args"]).description]
            query["expectedDigest"] = digest(expected)
            for connection, sql, label in [(source,query["sourceSql"],"sourceQueryPlan"),(projected,query["sql"],"projectionQueryPlan")]:
                plan = [r[3] for r in connection.execute("EXPLAIN QUERY PLAN " + sql, query["args"])]
                query[label] = {"details": plan, "scanPlanNodes": sum("SCAN " in row for row in plan), "searchPlanNodes": sum("SEARCH " in row for row in plan), "actualRowsScanned": None}
        for dataset,value in hashes.items():
            if file_hash(source_dir / (dataset + "_catalog.db")) != value:
                raise ValueError("Source changed during preparation")
        artifact = {"version":1,"sourceHashes":hashes,"tables":tables,"schemaStatements":statements,"queries":queries,"estimatedWrites":estimated,"seedRows":sum(t["rowCount"] for t in tables),"estimatedChunkCount":sum(math.ceil(t["rowCount"]/32) for t in tables),"parityScope":"Exact ordered SQL results over selected source IDs and complete sampled relationship closure; not full native parity","sqliteVersion":sqlite3.sqlite_version}
        artifact["seedContentDigest"] = hashlib.sha256(compact({"schema":statements,"tables":[{"name":t["name"],"columns":t["columns"],"rows":t["rows"]} for t in tables]}).encode()).hexdigest()
        artifact["seedRowsJsonBytes"] = sum(len(compact(t["rows"]).encode()) for t in tables)
        artifact["workloadDigest"] = hashlib.sha256(compact([{key:q[key] for key in ("id","sql","args","expectedDigest")} for q in queries]).encode()).hexdigest()
        output_dir.mkdir(parents=True, exist_ok=True)
        (output_dir / "schema.sql").write_text(";\n".join(statements)+";\n",encoding="utf-8")
        (output_dir / "seed.json").write_text(compact(artifact)+"\n",encoding="utf-8")
        (output_dir / "queries.json").write_text(json.dumps(queries,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        return artifact


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,default=ROOT/".publisher/benchmark")
    parser.add_argument("--max-estimated-writes",type=int,default=9000)
    args=parser.parse_args()
    result=prepare(args.source_dir,args.output_dir,args.max_estimated_writes)
    print(json.dumps({key:result[key] for key in ("seedContentDigest","seedRows","estimatedWrites","estimatedChunkCount","seedRowsJsonBytes")}))
