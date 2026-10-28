"""Model packet arrival lead, isolating lookahead within one native build."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess


def metrics(rows):
    result = {}
    for key in rows[0]:
        if not key.endswith('_ms'):
            continue
        values = sorted(float(row[key]) for row in rows)
        result[key] = dict(total=sum(values), p95=values[math.ceil(.95*len(values))-1],
                           p99=values[math.ceil(.99*len(values))-1], maximum=max(values))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--script', type=Path, required=True)
    parser.add_argument('--fonts', type=Path, required=True)
    parser.add_argument('--gold', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--first-frame', type=int, default=32700)
    parser.add_argument('--last-frame', type=int, default=32830)
    parser.add_argument('--burst-frame', type=int, default=32780)
    parser.add_argument('--rounds', type=int, default=2)
    parser.add_argument('--allowance-us', type=int, default=4000)
    parser.add_argument('--leads', type=int, nargs='+', default=[0, 250, 1000])
    args = parser.parse_args()
    if not 0 <= args.first_frame <= args.burst_frame <= args.last_frame:
        parser.error('Burst frame must be inside the nonnegative frame range')
    if any(lead < 0 or lead > 10000 for lead in args.leads):
        parser.error('Packet lead must be between zero and 10000 ms')
    if args.rounds < 1 or not 1 <= args.allowance_us <= 4000:
        parser.error('Invalid rounds or allowance')
    args.output.mkdir(parents=True, exist_ok=False)
    with args.gold.open(encoding='utf-8') as f:
        gold = {int(row['frame']): row for row in csv.DictReader(f)}
    result = dict(steady_scope='Only the first independently initialized model request is excluded from steady fields; all later burst/font/raster spikes stay. This is not an actual pause/resume test.', scope='Model of chronologically delivered Add/segment updates plus RenderEx, including initial active packets; source read/style setup excluded',
                  comparison=f'Same optimized native build, lookahead 0 versus {args.allowance_us} us; not an old-renderer speed comparison or observed playback delivery',
                  exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                  script_sha256=hashlib.sha256(args.script.read_bytes()).hexdigest(),
                  first_frame=args.first_frame, last_frame=args.last_frame, burst_frame=args.burst_frame, bitmap_cache_items=256, rounds=args.rounds, allowance_us=args.allowance_us, leads={})
    for lead in args.leads:
        record = dict(runs=[])
        result['leads'][str(lead)] = record
        for round_index in range(args.rounds):
            order = [('off', 0), ('on', args.allowance_us)]
            if round_index:
                order.reverse()
            for name, allowance in order:
                stem = f'lead{lead}-{round_index+1:02d}-{name}'
                output = args.output/(stem+'.csv')
                command = [str(args.exe.resolve()), str(args.script.resolve()), str(args.fonts.resolve()),
                           str(output.resolve()), str(lead), str(allowance), str(args.first_frame), str(args.last_frame)]
                print('Running', stem, flush=True)
                with (args.output/(stem+'.log')).open('w', encoding='utf-8') as log:
                    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
                log = (args.output/(stem+'.log')).read_text(encoding='utf-8')
                assert 'benchmark_cpu=7 affinity=0x80 priority=AboveNormal render_threads=1' in log
                assert 'bitmap_cache_items=256' in log
                with output.open(encoding='utf-8') as f:
                    rows = list(csv.DictReader(f))
                assert [int(row['frame']) for row in rows] == list(range(args.first_frame, args.last_frame+1))
                for row in rows:
                    expected = gold[int(row['frame'])]
                    assert all(row[key] == expected[key] for key in ('time_100ns', 'bitmaps', 'sha256')), row['frame']
                ahead = re.search(r'attempted=(\d+) retained=(\d+) prepare_ms=([\d.]+) largest_unit_ms=([\d.]+)', log)
                assert ahead
                if lead == 0 or allowance == 0:
                    assert int(ahead[1]) == 0, 'Native renderer received unavailable future events'
                record['runs'].append(dict(name=name, round=round_index+1, pixel_identical=True,
                    metrics=metrics(rows), steady_metrics=metrics(rows[1:]) if len(rows)>1 else None,
                    cold_first_frame={key: row[key] for row in rows[:1] for key in ('frame','render_ms','ingest_ms','frame_total_ms')},
                    prepared_events=int(ahead[1]), retained_events=int(ahead[2]),
                    preparation_ms=float(ahead[3]), largest_preparation_unit_ms=float(ahead[4]),
                    burst={key: float(row[key]) for row in rows if int(row['frame']) == args.burst_frame
                           for key in ('render_ms', 'ingest_ms', 'frame_total_ms', 'thread_cpu_ms')}))
                (args.output/'summary.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        record['median_pass_totals_ms'] = {}
        for key in record['runs'][0]['metrics']:
            values = {name: statistics.median(run['metrics'][key]['total'] for run in record['runs'] if run['name'] == name)
                      for name in ('off', 'on')}
            record['median_pass_totals_ms'][key] = dict(**values, reduction_percent=100*(1-values['on']/values['off']) if values['off'] else None)
        record['steady_median_maximum_ms'] = {name: statistics.median(run['steady_metrics']['frame_total_ms']['maximum'] for run in record['runs'] if run['name']==name) for name in ('off','on')} if args.last_frame>args.first_frame else None
        record['burst_median_ms'] = {key: {name: statistics.median(run['burst'][key] for run in record['runs'] if run['name'] == name)
                                          for name in ('off', 'on')}
                                     for key in ('render_ms', 'ingest_ms', 'frame_total_ms')}
        (args.output/'summary.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Packet lead comparisons completed; all frames match full-script gold.', flush=True)


if __name__ == '__main__':
    main()
