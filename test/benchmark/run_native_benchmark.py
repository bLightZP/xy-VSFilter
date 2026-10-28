"""Serial offline comparisons of the production renderer, with pixel gates."""
import argparse
import csv
import hashlib
import json
import math
import pathlib
import platform
import statistics
import subprocess

def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def summarize(rows):
    result = {}
    for key in rows[0]:
        if not key.endswith('_ms'):
            continue
        values = sorted(float(row[key]) for row in rows)
        def percentile(p):
            return values[max(0, math.ceil(len(values)*p)-1)]
        result[key] = dict(total=sum(values), mean=statistics.mean(values),
            median=statistics.median(values), p95=percentile(.95), p99=percentile(.99), maximum=max(values))
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=pathlib.Path, required=True)
    parser.add_argument('--control', type=pathlib.Path)
    parser.add_argument('--script', type=pathlib.Path, required=True)
    parser.add_argument('--fonts', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--rounds', type=int, default=2)
    parser.add_argument('--mode', choices=('render','parse','composite'), default='render')
    parser.add_argument('--first-frame', type=int, default=32128)
    parser.add_argument('--last-frame', type=int, default=34555)
    parser.add_argument('--bitmap-cache-items', type=int, default=256)
    args = parser.parse_args()
    if args.rounds < 1 or args.first_frame < 0 or args.last_frame < args.first_frame or not 0 <= args.bitmap_cache_items <= 1024:
        parser.error('Invalid rounds or frame range')
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = dict(mode=args.mode, host=platform.platform(), script_sha256=digest(args.script), first_frame=args.first_frame, last_frame=args.last_frame,
        cache_items=args.bitmap_cache_items, timings='Nested phases overlap; thread CPU clock has coarse per-frame resolution',
        fonts={p.name: digest(p) for p in sorted(args.fonts.iterdir()) if p.suffix.lower() in ('.ttf','.otf')}, runs=[])
    builds = {'candidate': args.candidate.resolve()}
    if args.control:
        builds['control'] = args.control.resolve()
    manifest['builds'] = {name: {'exe': str(exe), 'sha256': digest(exe)} for name,exe in builds.items()}
    reference = None
    scheduling = None
    for round_index in range(args.rounds):
        order = ['control','candidate'] if 'control' in builds else ['candidate']
        if round_index % 2:
            order.reverse()
        for name in order:
            stem = f'{round_index+1:02d}-{name}'
            output = args.output / (stem + '.csv')
            command = [str(builds[name]), '--benchmark', '--mode', args.mode, '--first-frame', str(args.first_frame),
                '--last-frame', str(args.last_frame), '--bitmap-cache-items', str(args.bitmap_cache_items)]
            if args.cpu is not None:
                command += ['--cpu', str(args.cpu)]
            command += [str(args.script.resolve()), str(args.fonts.resolve()), str(output.resolve())]
            print('Running', stem, flush=True)
            with (args.output/(stem+'.log')).open('w',encoding='utf-8') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            log_text = (args.output/(stem+'.log')).read_text(encoding='utf-8',errors='replace')
            setting = next((line for line in log_text.splitlines() if line.startswith('benchmark_cpu=')), None)
            if not setting or 'priority=AboveNormal render_threads=1' not in setting:
                raise RuntimeError('Benchmark scheduling was not verified')
            if scheduling is None:
                scheduling = setting
            if setting != scheduling:
                raise RuntimeError('CPU affinity or priority changed between runs')
            with output.open(encoding='utf-8') as stream:
                rows = list(csv.DictReader(stream))
            expected = args.last_frame - args.first_frame + 1
            if len(rows) != expected:
                raise RuntimeError(f'Expected {expected} frames; received {len(rows)}')
            pixels = [tuple(row[k] for k in ('frame','time_100ns','bitmaps','sha256')) for row in rows]
            if reference is None:
                reference = pixels
            if reference != pixels:
                raise RuntimeError('Rendered pixels differ from control')
            record = dict(name=name, round=round_index+1, scheduling=setting, pixel_identical=True if args.mode!='parse' else None, metrics=summarize(rows))
            manifest['runs'].append(record)
            (args.output/'summary.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    if args.control:
        manifest['comparison'] = {}
        keys = manifest['runs'][0]['metrics']
        for key in keys:
            totals = {name: statistics.median(r['metrics'][key]['total'] for r in manifest['runs'] if r['name']==name) for name in builds}
            manifest['comparison'][key] = dict(totals, reduction_percent=100*(1-totals['candidate']/totals['control']) if totals['control'] else None)
    (args.output/'summary.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('Completed parse timing (no pixels).' if args.mode=='parse' else 'Pixel gates passed.',
        'Results:', args.output/'summary.json')

if __name__ == '__main__':
    main()
