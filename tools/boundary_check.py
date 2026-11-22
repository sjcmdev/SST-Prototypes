"""Fast first-party C++ boundary inventory. Exceptions pin code, rule and count."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import time

RULES = {
    "text-json": r"\b(?:dump|Dump)\s*\(|\b(?:Json|json|nlohmann\s*::\s*json)\s*::\s*parse\s*\(",
    "text-yaml": r"\bYAML\s*::\s*(?:Load(?:File)?|Dump|Emitter|Node)\b|\bNode\s*\(",
    "text-stream": r"\b(?:std\s*::\s*)?(?:o|string)stringstream\b",
    "text-number": r"\bstd\s*::\s*sto[fdl]+\s*\(",
    "packed-result": r"\bPack\s*\(|\bPJson\s*[({]",
    "json-api": r"\b\w+_json\s*\(",
    "python": r"\b(?:nb|py|nanobind|pybind11)\s*::|\bPyObject\b|\bPy\w+\s*\(|\b(?:scoped_interpreter|PythonInterpreter)\b",
}
PATTERNS = {name: re.compile(pattern) for name, pattern in RULES.items()}
# Keep strings (arguments) for stable fingerprints, but do not scan their contents.
TOKENS = re.compile(r'R"(?P<delim>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delim)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)


def scrub(source: str, strings: bool = True) -> str:
    def replace(match):
        token = match.group()
        if not strings and not token.startswith(("//", "/*")):
            return token
        return "".join("\n" if c == "\n" else " " for c in token)
    return TOKENS.sub(replace, source)


def inventory(root: Path, config: dict) -> list[dict]:
    hits = []
    paths = set()
    for directory in config["scan"]:
        paths.update(p for p in (root / directory).rglob("*")
                     if p.suffix in {".cpp", ".hpp", ".h", ".cc", ".cxx"}
                     and not any(part in {"Vendor", "vendor", "build", "_deps", "third_party"} for part in p.relative_to(root).parts))
    for path in sorted(paths):
        source = path.read_text(encoding="utf-8-sig")
        clean = scrub(source)
        lines = scrub(source, strings=False).splitlines()
        for rule, pattern in PATTERNS.items():
            for match in pattern.finditer(clean):
                line = clean.count("\n", 0, match.start()) + 1
                code = re.sub(r"\s+", " ", lines[line - 1]).strip()
                hits.append({"file": path.relative_to(root).as_posix(), "rule": rule,
                             "line": line, "code": code,
                             "sha256": hashlib.sha256(code.encode()).hexdigest()})
    return hits


def key(hit):
    return hit["file"], hit["rule"], hit["sha256"]


def check(root: Path, config: dict) -> int:
    start = time.perf_counter()
    allowed = Counter()
    for section in ("edges", "known-debt"):
        for entry in config[section]:
            if not entry.get("reason", "").strip():
                raise ValueError("Every exception requires a reason")
            if section == "known-debt" and not re.search(r"\bF\d{2}\b", entry["reason"]):
                raise ValueError("Known debt requires an audit finding ID")
            allowed[key(entry)] += entry.get("count", 1)
    hits = inventory(root, config)
    failures = []
    for hit in hits:
        if allowed[key(hit)]:
            allowed[key(hit)] -= 1
        else:
            failures.append(hit)
    for hit in failures:
        print(f'{hit["file"]}:{hit["line"]}: {hit["rule"]}: {hit["code"]}')
    print(f'boundary-check: {len(hits)} hits, {len(config["known-debt"])} debt entries, '
          f'{len(failures)} NEW violations; {time.perf_counter() - start:.3f}s')
    return bool(failures)


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory(prefix="sst-boundary-") as directory:
        root = Path(directory)
        file = root / "solver.cpp"
        file.write_text('// Json::parse(x.dump());\nconst char* s="x.dump()";\n', encoding="utf-8")
        config = {"scan": ["."], "edges": [], "known-debt": []}
        assert not inventory(root, config), "comments and literals are not calls"
        file.write_text('auto y = Json::parse(x.dump());\n', encoding="utf-8")
        hits = inventory(root, config)
        assert len(hits) == 2 and check(root, config), "bad solver roundtrip must fail"
        config["edges"] = [dict(h, reason="Self-test file adapter") for h in hits]
        assert not check(root, config)
        file.write_text(file.read_text() * 2, encoding="utf-8")
        assert check(root, config), "duplicating an allowed call must fail"
        for rule, sample in {"text-yaml": 'auto n=YAML::Load(text);',
                             "text-stream": 'std::ostringstream data;',
                             "text-number": 'auto x=std::stod(text);',
                             "packed-result": 'auto x=Pack(result);',
                             "json-api": 'run_batch_json(a,b);',
                             "python": 'nb::module_::import("solver");'}.items():
            file.write_text(sample, encoding="utf-8")
            assert any(h["rule"] == rule for h in inventory(root, config)), rule
    print("boundary-check self-test: PASS (deliberate bad patch rejected)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    config = json.loads((args.root / "tools/boundary_allowlist.json").read_text(encoding="utf-8"))
    if args.inventory:
        print(json.dumps(inventory(args.root, config), indent=2))
        return 0
    return check(args.root, config)


if __name__ == "__main__":
    sys.exit(main())
