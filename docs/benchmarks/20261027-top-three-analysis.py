"""Summarize frozen baseline/candidate ABBA CSVs without rendering or playback."""
import argparse,csv,hashlib,json,math,re,statistics
from pathlib import Path

SPIKES=[32249,32693,32780,32854,33157,33448,33973]
BUDGET=1001/24
PHASES=['render_ms','parse_ms','build_draw_ms','composite_ms','tree_ms','merge_ms','group_ms','bitmap_ms']
COUNTERS=['input_items','groups','member_references','draw_items','draw_reuse_hits','draw_reuse_misses','machine_allocations','draw_allocations','overlay_key_allocations','draw_metadata_bytes']
def load(p):
 with p.open(encoding='utf-8') as f:return list(csv.DictReader(f))
def stamp(frame):
 ms=(2*frame+1)*1001/48
 return f'{int(ms//60000):02}:{ms/1000%60:06.3f}'
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);ap.add_argument('--output',type=Path)
 args=ap.parse_args();root=args.root.resolve();base=root/'benchmark-results/top-three-20261027'
 cohort=json.loads((root/'benchmark-results/event-layout-20261027/burst-cohort.json').read_text(encoding='utf-8'))['frames']
 selected=sorted(set(SPIKES+[r['frame'] for r in cohort]))
 data={'scope':'Native console only; no playback or measured dropped frames. Complete RenderEx includes bounded lookahead; nested phases overlap. Four fresh-process passes per build/architecture in ABBAABBA order; CPU7/0x80 AboveNormal, one rendering thread. Source frame midpoint at 24000/1001 fps.','frame_budget_ms':BUDGET,'input_sha256':hashlib.sha256((root/'benchmark-results/media/default.ass').read_bytes()).hexdigest(),'baseline':'Frozen preceding accepted G build; all 47 source hashes and eight DLL/console/PDB hashes verified before editing.','spike_definition':'Per-frame median across four passes. Following-120-ms windows include activation and next two requests (0,41.708,83.417 ms); request +125.125 ms recorded separately.','architectures':{}}
 for arch in ['x64','Win32']:
  folder=base/f'abba-{arch}';record={'runs':{},'summary':{},'spikes':[],'fixed_bursts':[],'logs':{}};data['architectures'][arch]=record
  captured=json.loads((folder/'summary.json').read_text(encoding='utf-8'))
  record['benchmark_builds']=captured['builds']
  final_manifest=base/f'stable-{arch}/build-manifest.json'
  if final_manifest.exists():
   final=json.loads(final_manifest.read_text(encoding='utf-8'))
   record['final_sources']=final['sources'];record['final_binaries']=final['binaries']
   record['validation']=final.get('validation')
  tables={};maps={}
  for name in ['control','candidate']:
   files=sorted(folder.glob('*-'+name+'.csv'));assert len(files)==4,(arch,name,len(files))
   rows=[load(p) for p in files];tables[name]=rows;maps[name]=[{int(r['frame']):r for r in rs} for rs in rows]
   assert all(len(rs)==2428 for rs in rows)
   record['runs'][name]=[]
   logs=[]
   for file,rs in zip(files,rows):
    values=sorted(float(r['render_ms']) for r in rs)
    record['runs'][name].append({'csv':str(file.relative_to(root)).replace('\\','/'),'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'render_total_ms':sum(values),'thread_cpu_ms':sum(float(r['thread_cpu_ms']) for r in rs),'p99_ms':values[math.ceil(.99*len(values))-1],'max_ms':max(values),'over_budget':sum(v>BUDGET for v in values)})
    logs.append(file.with_suffix('.log').read_text(encoding='utf-8'))
   record['summary'][name]={k:statistics.median(r[k] for r in record['runs'][name]) for k in ['render_total_ms','thread_cpu_ms','p99_ms','max_ms','over_budget']}
   for key in ['peak_pagefile_bytes','process_private_bytes','peak_bytes','peak_accounted_bytes','largest_unit_ms','cache_overlay_queries','prepared_layout_hits','draw_reuse_hits','machine_allocations','draw_allocations','overlay_key_allocations']:
    vals=[]
    for log in logs:
     m=re.search(r'(?<![a-zA-Z_])'+key+r'=([0-9.]+)',log)
     if m:vals.append(float(m[1]))
    if vals:record['logs'].setdefault(name,{})[key]=statistics.median(vals)
   for prefix in ['lookahead_','prepared_layout_','draw_reuse_','reconstruction_','cache_overlay_']:
    line=next((line for line in logs[0].splitlines() if line.startswith(prefix)),None)
    if line:record['logs'].setdefault(name,{})[prefix+'first_pass']=line
  reference=[tuple(r[k] for k in ['frame','time_100ns','bitmaps','sha256']) for r in tables['control'][0]]
  for rows in sum(tables.values(),[]):assert reference==[tuple(r[k] for k in ['frame','time_100ns','bitmaps','sha256']) for r in rows]
  def frame_metrics(frame,name):
   rs=[m[frame] for m in maps[name]]
   result={key:statistics.median(float(r[key]) for r in rs) for key in PHASES+COUNTERS if all(key in r for r in rs)}
   result['render_samples_ms']=[float(r['render_ms']) for r in rs]
   return result
  for frame in selected:
   item={'frame':frame,'position':stamp(frame),'control':frame_metrics(frame,'control'),'candidate':frame_metrics(frame,'candidate'),'following':[],'window_0_through_83ms':{}}
   item['render_reduction_percent']=100*(1-item['candidate']['render_ms']/item['control']['render_ms'])
   for offset in [1,2,3]:item['following'].append({'frame':frame+offset,'offset_ms':offset*BUDGET,'control':frame_metrics(frame+offset,'control'),'candidate':frame_metrics(frame+offset,'candidate')})
   for name in ['control','candidate']:
    item['window_0_through_83ms'][name]=statistics.median(sum(float(m[frame+i]['render_ms']) for i in [0,1,2]) for m in maps[name])
   record['fixed_bursts'].append(item)
   if frame in SPIKES:record['spikes'].append(item)
  for metric in ['thread_cpu_ms','render_total_ms']:
   a=record['summary']['control'][metric];b=record['summary']['candidate'][metric];record['summary'][metric+'_reduction_percent']=100*(1-b/a)
  record['pixel_mismatches']=0
  packet=base/f'packets-{arch}/summary.json'
  if packet.exists():record['packet_model']=json.loads(packet.read_text(encoding='utf-8'))
 out=args.output or root/'docs/benchmarks/20261027-top-three.json'
 out.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
 print(out)
 for arch,r in data['architectures'].items():
  print(arch,r['summary'])
  for f in r['spikes']:print(f['position'],round(f['control']['render_ms'],3),round(f['candidate']['render_ms'],3),round(f['render_reduction_percent'],1))
if __name__=='__main__':main()
