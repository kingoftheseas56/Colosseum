"""Read-only SQLite inventory and lossless, namespaced D1 schema generation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sqlite3

DATASETS = ("imdb", "mal", "comics", "tankoban")


def prepare(source_dir, output_dir):
    tables, statements, sources = {}, [], {}
    for dataset in DATASETS:
        path = source_dir / f"{dataset}_catalog.db"
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        db = sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)
        sources[dataset] = {"sha256": digest, "bytes": path.stat().st_size}
        for name, ddl in db.execute("SELECT name,sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name"):
            if not re.fullmatch(r"[a-zA-Z_][a-zA-Z_0-9]*", name):
                raise ValueError("Unexpected source table name")
            target = dataset + "__" + name
            columns = [r[1] for r in db.execute(f'PRAGMA table_info("{name}")')]
            if any(not re.fullmatch(r"[a-zA-Z_][a-zA-Z_0-9]*", c) for c in columns):
                raise ValueError("Unexpected column name")
            indexes = list(db.execute(f'PRAGMA index_list("{name}")'))
            # Preserve table definitions, primary keys, constraints and all source columns.
            ddl = re.sub(r'^CREATE TABLE\s+(?:IF NOT EXISTS\s+)?(?:"' + name + '"|' + name + r')\s*',
                         f'CREATE TABLE IF NOT EXISTS "{target}" ', ddl, count=1, flags=re.I)
            if not ddl.startswith(f'CREATE TABLE IF NOT EXISTS "{target}" '):
                raise ValueError("Unsupported table declaration")
            if "REFERENCES" in ddl.upper():
                raise ValueError("Foreign keys require explicit namespaced mapping")
            statements.append(ddl + ";")
            for _, index_name, *_ in indexes:
                index_sql = db.execute("SELECT sql FROM sqlite_master WHERE name=?", (index_name,)).fetchone()[0]
                if index_sql is None:
                    continue
                match = re.search(r'\bON\s+(?:"' + name + '"|' + name + r')\s*(.*)$', index_sql, re.I | re.S)
                if not match:
                    raise ValueError("Unsupported index declaration")
                unique = "UNIQUE " if re.match(r"CREATE\s+UNIQUE", index_sql, re.I) else ""
                statements.append(f'CREATE {unique}INDEX IF NOT EXISTS "{dataset}__{index_name}" ON "{target}" {match[1]};')
            tables[target] = {"dataset": dataset, "sourceTable": name, "columns": columns,
                              "rowCount": db.execute(f'SELECT COUNT(*) FROM "{name}"').fetchone()[0],
                              "sourceHash": digest, "writeWeight": 1 + len(indexes)}
        db.close()
    (output_dir / "src").mkdir(parents=True, exist_ok=True)
    (output_dir / "migrations/catalog").mkdir(parents=True, exist_ok=True)
    (output_dir / "src/source-schema.json").write_text(json.dumps(tables, indent=2) + "\n", encoding="utf-8")
    (output_dir / "migrations/catalog/0002_sources.sql").write_text("\n".join(statements) + "\n", encoding="utf-8")
    seeds = ["INSERT OR IGNORE INTO _source_tables(name,source_hash,expected_rows) VALUES ('%s','%s',%d);" %
             (name, table["sourceHash"], table["rowCount"]) for name, table in tables.items()]
    (output_dir / "migrations/catalog/0003_manifest.sql").write_text("\n".join(seeds) + "\n", encoding="utf-8")
    report = {"sources": sources, "sourceBytes": sum(x["bytes"] for x in sources.values()),
              "tables": len(tables), "rows": sum(x["rowCount"] for x in tables.values()),
              "estimatedRowWrites": sum(x["rowCount"] * x["writeWeight"] for x in tables.values())}
    (output_dir / "source-manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    print(json.dumps(prepare(args.source_dir, args.output_dir), indent=2))
