"""Inventory the exact upstream inputs recorded by clang, not the entire checkout.

Produces evidence for human license review; deliberately does not issue a legal
compliance verdict. Unknown components fail closed. No output goes into APKs.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def component(path):
    if path.startswith('frontend/libpicofe/'):
        raise ValueError(f'Unused desktop submodule entered the build: {path}')
    for prefix, name in (
        ('deps/libchdr/deps/lzma-', 'lzma'),
        ('deps/libchdr/deps/zstd-', 'zstd'),
        ('deps/libchdr/include/dr_libs/', 'dr_flac'),
        ('deps/libchdr/', 'libchdr'),
        ('deps/libretro-common/', 'libretro-common'),
        ('deps/crypto/', 'crypto'),
        ('libpcsxcore/new_dynarec/', 'ari64'),
        ('libpcsxcore/', 'pcsx'),
        ('plugins/dfsound/', 'dfsound'),
        ('plugins/gpu_neon/', 'gpu_neon'),
        ('plugins/gpulib/', 'gpulib'),
        ('plugins/cdrcimg/', 'cdrcimg'),
        ('frontend/', 'frontend-libretro'),
        ('include/', 'pcsx-headers'),
    ):
        if path.startswith(prefix):
            return name
    raise ValueError(f'Unreviewed component: {path}')


def dependency_paths(text):
    # NDK make paths are required to be space-free by our build recipe. Parse
    # only the first target: -MP emits additional empty header targets below.
    rule = text.replace('\\\n', ' ').splitlines()[0]
    _, deps = re.split(r':\s+', rule, maxsplit=1)
    return deps.split()


def inventory(source, output):
    lock = json.loads((ROOT / 'config/ps1-core.lock.json').read_text())
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != lock['revision']:
        raise ValueError('Source revision differs from lock')
    units, paths = set(), set()
    for dep in sorted((output / 'obj').rglob('*.o.d')):
        dependencies = dependency_paths(dep.read_text(encoding='utf-8'))
        for index, item in enumerate(dependencies):
            absolute = Path(item).resolve()
            if not absolute.is_relative_to(source):
                raise ValueError(f'Unexpected non-upstream dependency: {absolute}')
            relative = absolute.relative_to(source).as_posix()
            paths.add(relative)
            if index == 0:
                units.add(relative)
    if not units:
        raise ValueError('No compiler dependency files; build first')
    records = []
    for path in sorted(paths):
        blob = (source / path).read_bytes()
        records.append(dict(path=path, component=component(path), translation_unit=path in units,
                            sha256=hashlib.sha256(blob).hexdigest()))
    return dict(revision=revision, translation_units=len(units), source_inputs=len(records),
                components=dict(sorted(Counter(r['component'] for r in records).items())),
                scope='Compiled translation units + transitive project includes. NDK system headers excluded by clang -MMD.',
                legal_verdict='NOT_ISSUED; inspect per-file notices and linked recipe', files=records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inventory(args.source.resolve(), args.build.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'files'}))


if __name__ == '__main__':
    main()
