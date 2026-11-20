"""Run each GPU gate under its own build-lock; aggregate on the CPU outside the lock."""
import argparse
import csv
import json
import math
import struct
import shutil
import subprocess
from pathlib import Path


def run_case(command, output, resume, case, mode, method, samples, repeats):
    binary = Path(str(output)+'.chi2.bin')
    if resume and output.exists() and binary.exists():
        try:
            row = json.loads(output.read_text())
        except json.JSONDecodeError:
            row = {}
        expected = dict(case=case, mode=mode, method=method, samples=samples, repeats=repeats,
                        result_mismatches=0, current_mismatches=0)
        if all(row.get(key) == value for key, value in expected.items()) and binary.stat().st_size == samples*8:
            return row
    subprocess.run(command, check=True)
    return json.loads(output.read_text())


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('captures', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--resume', action='store_true', help='Reuse complete matching results; verify source provenance first')
    args = parser.parse_args()
    studio = Path(__file__).resolve().parents[5]
    lock = studio / 'tools/agents/build-lock.sh'
    # An absolute executable path avoids Windows selecting the WSL bash from System32.
    bash = shutil.which('bash')
    if bash is None:
        raise FileNotFoundError('Git Bash is required for build-lock')
    args.output.mkdir(parents=True, exist_ok=True)
    # A one-iteration budget forces the actual LM -> NM branch, including a partial block.
    forced = json.loads((args.captures/'HZB_T330_4p.json.resolved.json').read_text())
    forced['request']['max_iter'] = 1
    forced['id'] += '_forced_fallback'
    capture = args.output/'forced-fallback.resolved.json'
    capture.write_text(json.dumps(forced))
    for mode in ('log_legacy', 'isc_shift', 'signed_sigma'):
        output = args.output/f'forced-fallback-{mode}.json'
        command = [bash, lock.as_posix(), args.executable.resolve().as_posix(), capture.resolve().as_posix(), mode, 'lm',
                   '133', '1', output.resolve().as_posix()]
        row = run_case(command, output, args.resume, forced['id'], mode, 'lm', 133, 1)
        assert row['lm_fallback'] > 0 and row['result_mismatches'] == row['current_mismatches'] == 0, row
    rows = []
    for case in ('HZB_T330_4p', 'HZB_T181_6p', 'HZB_T247_7p', 'synthetic_forward_4p'):
        capture = args.captures / f'{case}.json.resolved.json'
        for mode in ('log_legacy', 'isc_shift', 'signed_sigma'):
            for n in (1000, 20000):
                objectives = {}
                for method in ('lm', 'nm'):
                    output = args.output / f'{case}-{mode}-{method}-{n}.json'
                    command = [bash, lock.as_posix(), args.executable.resolve().as_posix(), capture.resolve().as_posix(), mode, method,
                               str(n), str(args.repeats), output.resolve().as_posix()]
                    row = run_case(command, output, args.resume, case, mode, method, n, args.repeats)
                    assert row['result_mismatches'] == row['current_mismatches'] == 0, row
                    rows.append(row)
                    objectives[method] = struct.unpack(f'{n}d', Path(str(output)+'.chi2.bin').read_bytes())
                passed = sum(math.isfinite(lm) and math.isfinite(nm) and lm <= nm*(1+1e-12)
                             for lm, nm in zip(objectives['lm'], objectives['nm']))
                for row in rows[-2:]:
                    row['lm_nm_chi2_passed'] = passed
                with (args.output/'speedups.csv').open('w', newline='') as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
                assert passed == n, f'{case}/{mode}/N={n}: chi2 LM <= NM*(1+1e-12): {passed}/{n}'
    summary = dict(rows=len(rows), sample_pairs=sum(row['samples'] for row in rows),
                   result_mismatches=sum(row['result_mismatches'] for row in rows),
                   current_mismatches=sum(row['current_mismatches'] for row in rows),
                   lm_nm_chi2_passed=sum(row['lm_nm_chi2_passed'] for row in rows if row['method']=='lm'))
    (args.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
