"""Differential M304/M663 default-options parity against the hash-locked bundle.

The production candidate is native C++; Node is used only for this test oracle.
Node 24 is the default gate. Node 22 requires the explicit portable-runtime flag.
"""
import argparse
import hashlib
import itertools
import json
import pathlib
import random
import subprocess


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--oracle", required=True)
    parser.add_argument("--index", required=True)
    parser.add_argument("--node", default="node")
    parser.add_argument("--allow-portable-node22", action="store_true")
    parser.add_argument("--random-cases", type=int, default=4000)
    return parser.parse_args()


def corpus(random_cases):
    names = [
        "", "/", "\\", "///", ".mkv", "mkv", "amkv", "movie.mkv", "movie.MKV",
        "movie.mkv\n", "movie.mkv\r", "movie.mkv\u2028", "movie.mkv\u2029",
        "a\nb.mkv", "a\rb.mkv", "\nmkv", "\rmkv", "\u2028mkv", "\u2029mkv",
        "😀mkv", "movie.mkv/", "Movie.S01E02.mkv", "S01E02.mkv", "S01E02",
        "Name.S01E01E02.mkv", "Name.S01E02-E03.mkv", "Name.S01.E02.mkv",
        "Name.S01.e02.mkv", "Name.E02.S01.mkv", "Name.S1E2.mkv",
        "Name.S123E456.mkv", "Name.1x2.mkv", "Name.123x456.mkv",
        "Name.01.02.mkv", "Name.01.02.2012.mkv", "Name.2012.01.02.mkv",
        "Name.2012.1.2.mkv", "Name.2012.S01E02.mkv", "Name.S00E00.mkv",
        "Name.S01E00.mkv", "Name.S00E02.mkv", "Name.S01E02.1080p.mkv",
        "Name.2031.S01E02.mkv", "Name.2030.S01E02.mkv", "Name.1900.S01E02.mkv",
        "Name.1899.S01E02.mkv", "Name.103.mkv", "Name.1003.mkv",
        "Name.s103.mkv", "Name.103e.mkv", "Name.103E.mkv", "Name.S103.mkv",
        "Name.2012.103.mkv", "Name.103.2012.mkv", "Name.2012.s103.mkv",
        "Name.2012.103e.mkv", "Name.-13.mkv", "Name.+13.mkv", "Name.0x1.mkv",
        "Name.0b1.mkv", "Name.0o1.mkv", "Name.1e2.mkv", "Name.Inf.mkv",
        "Name.nan.mkv", "Name. 13.mkv", "Name.\ufeff13.mkv", "Name.\u00a013.mkv",
        "Name/Season 1/Episode 2.mkv", "Name/Season 1 - 2.mkv", "Name/season1/ep2.mkv",
        "Name/Season.1/Ep.2.mkv", "Name/season_1/Episode_2.mkv",
        "Name/S02E03/S01E02.mkv", "Name/S02E03/Movie.S01E02.mkv",
        "Outer/S02E03/Inner/Movie.S01E02.mkv", "Name/Sample/S01E02.mkv",
        "Name/SAMPLE/S01E02.mkv", "Name/sampleMovie.S01E02.mkv", "Name/etrg.S01E02.mkv",
        "Name/S01E02/sample.mkv", "[Source] Name.S01E02.mkv", "[Source]\nName.S01E02.mkv",
        "A&B.S01E02.mkv", "A-B.S01E02.mkv", "[Source].Name.S01E02.mkv",
        "Name.(Cut).S01E02.mkv", "Name.1080p.720p.480p.mkv", "Name.cd1.mkv",
        "Name.CD1.mkv", "Name.part1.mkv", "Name.pt1.mkv", "Name.disk1.mkv",
        "Name.12foo.mkv", "Name.NaN.S01E02.mkv", "Name.Infinity.S01E02.mkv",
        "K.S01E02.mkv", "ſ.S01E02.mkv", "İ.S01E02.mkv", "Épisode.S01E02.mkv",
        "Name.S01\u2028E02.mkv", "Name.S01😀E02.mkv", "Name.S01١E02.mkv",
        "Name.S01_E02.mkv", "Name.S01-E02.mkv", "Name.S01.E02.mkv",
    ]
    prefixes = ["Show", "", "Show/", "Show/Season 1/", "Sample/", "Show.2012/", "2012/",
                "S02E03/", "Show/S02E03/", "Show/2012-01-02/", "one/two/three/four/",
                "[source]", "[source].", "Name&Name.", "Name\n", "Name\t"]
    stamps = ["S01E02", "S01E02E03", "S01E2", "S01.E02", "S01-E02", "S01_E02",
              "S00E02", "S01E00", "S01E020", "s1e02", "1x02", "1x2", "1.02", "01.2",
              "102", "0102", "s102", "102e", "s102e", "2012.102", "102.2012",
              "Season 1 - 2", "season1.episode2", "Season 1.Ep2", "\u00a012", "0x2",
              "1e2", "NaN", "Inf", "\t12", "-12", "+12", "Episode2", "S01"]
    extensions = [".mkv", ".MP4", ".txt", ".mp3", ".ts", "mkv", ".mkv\n", ".mkv\u2028"]
    names += [prefix + stamp + extension for prefix, stamp, extension in itertools.product(prefixes, stamps, extensions)]
    randomizer = random.Random(663304)
    tokens = ["Name", "Movie", "Title", "sample", "etrg", "2012", "2031", "S01", "S02",
              "E01", "E02", "S01E02", "S02E03", "season1", "Season 2 - 3", "1x02",
              "2x03", "103", "s103", "103e", "1080p", "720p", "cd1", "episode2",
              "0x1", "1e2", "+12", "-12", "INF", "NaN", "Infinity", "[Group]", "é",
              "ß", "ſ", "K", "İ", "١", "😀", "", "\ufeff", "\t", "\n", "\r", "\u2028", "\u2029"]
    delimiters = [".", " ", "_", "-", ";", "/", "\\", "", ".."]
    for _ in range(random_cases):
        path = randomizer.choice(tokens)
        for _ in range(randomizer.randrange(1, 9)):
            path += randomizer.choice(delimiters) + randomizer.choice(tokens)
        names.append(path + randomizer.choice(extensions))
    names = list(dict.fromkeys(names))
    cases = [{"mode": "parse", "path": name} for name in names]
    infos = [{"season": 1, "episode": 2}, {"season": 2, "episode": 3},
             {"season": "1", "episode": 2}, {"season": 1, "episode": "2"},
             {"season": 0, "episode": 2}, {"season": 1, "episode": 0}, {}, True]
    for index, name in enumerate(names):
        cases.append({"files": [{"path": "Fallback.mp4", "length": 1000}, {"path": name, "length": 1}],
                      "seriesInfo": infos[index % len(infos)]})
    for extension in ["mkv", "avi", "mp4", "wmv", "vp8", "mov", "mpg", "ts", "m3u8", "webm",
                      "flac", "mp3", "wav", "wma", "aac", "ogg", "m4v", "vob", "m2ts"]:
        for before, after in itertools.product(["", ".", "x", "\n", "\r", "\u2028", "\u2029", "😀", "a\nb."], ["", "\n", "\r", "\u2028", "\u2029"]):
            cases.append({"files": [{"path": before + extension.upper() + after, "length": 1}], "seriesInfo": {}})
    values = [None, False, True, 0, 1, -1, 1.5, "", "1", "2", "10", "100", [], [1], [2],
              [10], [None], [1, 2], {}, {"toString": 1}, {"valueOf": 1}, {"x": 1},
              {"$number": "NaN"}, {"$number": "Infinity"}, {"$number": "-Infinity"},
              {"$undefined": True}, "\ufeff2", "2\u00a0", "inf", "-0x1", "1e2", "1e1000", "1e-1000",
              [0.1], [1e20], [1e21], [1e-6], [1e-7], [1000000000000000100], [[1, 2], [None]],
              [1.0000000000000002], [1.7976931348623157e308], [5e-324]]
    for a, b in itertools.product(values, values):
        cases.append({"files": [{"path": "first.mkv", "length": a}, {"path": "second.mkv", "length": b}], "seriesInfo": {}})
    for bits in [52, 53, 54, 60, 63, 64, 65, 100, 1023, 1024, 1025]:
        for offset in [-3, -1, 0, 1, 2, 3, 2 ** max(0, bits - 54), 2 ** max(0, bits - 54) + 1]:
            integer = 2 ** bits + offset
            try:
                rounded = float(integer)
            except OverflowError:
                rounded = {"$number": "Infinity"}
            for text in [hex(integer), oct(integer), bin(integer)]:
                for a, b in [(text, rounded), (rounded, text)]:
                    cases.append({"files": [{"path": "first.mkv", "length": a}, {"path": "second.mkv", "length": b}], "seriesInfo": {}})
    files = [None, False, True, 1, "files", {}, [], [None], [False], [0], ["x"], [{}],
             [{"path": None}], [{"path": 1}], [{"path": []}], [{"path": "Name.S01E02.mkv"}],
             [{"path": "Name.S01E02.mkv", "length": 1}, {"path": "Other.mkv", "length": 2}],
             [{"path": "Name.S01E02.mkv", "length": 1}, {"path": "Name.S01E02.mp4", "length": 1}]]
    for filelist, info in itertools.product(files, values + infos):
        cases.append({"files": filelist, "seriesInfo": info})
    cases += [{"files": filelist, "seriesInfo": {}, "errorDetails": True} for filelist in files]
    cases += [{"mode": "parse", "path": "", "errorDetails": True}]
    cases += [{}, {"files": [{"path": "x.mkv"}]}, {"files": [], "seriesInfo": {}}]
    return cases


def main():
    args = arguments()
    version = subprocess.check_output([args.node, "--version"], text=True).strip()
    major = int(version.lstrip("v").split(".")[0])
    if major != 24 and not (major == 22 and args.allow_portable_node22):
        raise SystemExit(f"Oracle requires Node 24; Node 22 needs --allow-portable-node22 (found {version})")
    bundle = pathlib.Path(args.oracle).read_bytes()
    digest = hashlib.sha256(bundle).hexdigest()
    assert digest == "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f", digest
    index = json.loads(pathlib.Path(args.index).read_text())
    assert index["oracle_sha256"] == digest
    factories = {}
    for module in index["modules"]:
        if module["id"] in (304, 663):
            factory = bundle[module["start_byte"]:module["end_byte"]]
            assert hashlib.sha256(factory).hexdigest() == module["sha256"]
            assert module["dependencies"] == ([] if module["id"] == 304 else [304])
            factories[module["id"]] = factory.decode()
    script = "const factories={" + ",".join(f"{key}:({value})" for key, value in factories.items()) + "};\n"
    script += r"""
const cache = {};
function load(id) {
  if (cache[id]) return cache[id].exports;
  const module = cache[id] = {exports:{}};
  factories[id](module,module.exports,load);
  return module.exports;
}
function revive(value) {
  if (!value || typeof value !== 'object') return value;
  if (!Array.isArray(value) && Object.keys(value).length === 1) {
    if ('$number' in value) return Number(value.$number);
    if (value.$undefined) return undefined;
  }
  if (Array.isArray(value)) return value.map(revive);
  const result = {};
  for (const key of Object.keys(value)) Object.defineProperty(result,key,{value:revive(value[key]),enumerable:true,writable:true,configurable:true});
  return result;
}
for (const line of require('fs').readFileSync(0,'utf8').trimEnd().split('\n')) {
  const input = revive(JSON.parse(line));
  try {
    const result = input.mode === 'parse' ? load(304)(input.path) : load(663)(input.files,input.seriesInfo);
    process.stdout.write(JSON.stringify({ok:result})+'\n');
  } catch(error) {
    process.stdout.write(JSON.stringify(input.errorDetails ? {error:error.name,message:error.message} : {error:error.name})+'\n');
  }
}
"""
    cases = corpus(args.random_cases)
    payload = "".join(json.dumps(case, ensure_ascii=True, separators=(",", ":")) + "\n" for case in cases)
    def run(command):
        result = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise SystemExit(f"Command failed ({result.returncode}): {command[0]}\n{result.stderr}")
        return result.stdout.splitlines()
    expected = run([args.node, "-e", script])
    actual = run([args.candidate])
    assert len(expected) == len(actual) == len(cases), (len(expected), len(actual), len(cases))
    failures = []
    for case, want, got in zip(cases, expected, actual):
        if json.loads(want) != json.loads(got):
            failures.append({"case": case, "expected": json.loads(want), "actual": json.loads(got)})
    if failures:
        print(json.dumps(failures[:25], indent=2, ensure_ascii=True))
        raise SystemExit(f"FAIL {len(failures)}/{len(cases)} M304/M663 cases")
    parse_count = sum(case.get("mode") == "parse" for case in cases)
    gate = "Node24" if major == 24 else "portable-Node22"
    print(f"PASS {len(cases)} exact M304/M663 cases ({parse_count} complete default-options metadata, {len(cases)-parse_count} selection); {gate} {version}; bundle {digest}")


if __name__ == "__main__":
    main()
