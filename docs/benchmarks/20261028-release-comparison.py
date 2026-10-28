"""Requires the locally preserved original/v900 executables; build v910 first. No playback."""
from pathlib import Path
import ctypes,csv,hashlib,json,math,statistics,subprocess,sys,time

ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'benchmark-results/release-comparison-reproduction'
BASE.mkdir(exist_ok=False)
sys.path.insert(0,str(ROOT/'test/benchmark'))
from compare_rendered import load

k=ctypes.WinDLL('kernel32',use_last_error=True)
k.GetPriorityClass.argtypes=[ctypes.c_void_p];k.GetPriorityClass.restype=ctypes.c_ulong
k.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
assert k.SetProcessAffinityMask(ctypes.c_void_p(-1),0x20)
assert k.SetPriorityClass(ctypes.c_void_p(-1),0x20)

builds={
 'original-Win32':ROOT/'benchmark-results/source-control/render_subtitles.exe',
 'v900-Win32':ROOT/'benchmark-results/word-keys-exact/render_subtitles.exe',
 'v910-Win32':ROOT/'bin/lib_18.0/Win32/Release/render_subtitles.exe',
 'v910-x64':ROOT/'bin/lib_18.0/x64/Release/render_subtitles.exe',
}
result={'release':'v3.2.1.910','method':{
 'logical_cpu':5,'affinity_mask':'0x20','priority':'AboveNormal','render_threads':1,
 'bitmap_cache_items':64,'render_size':[1920,1080],'fps':'24000/1001',
 'frames':[32128,34555],'frame_count':2428,
 'timing_scope':'Per-request QueryPerformanceCounter around RenderEx only; input loading, font loading and bitmap SHA256 excluded. New renderer includes synchronous lookahead.',
 'order':'original, v900, v910-Win32, v910-x64; reverse order; two fresh processes per executable',
 'baseline_provenance':'Original fork native Win32 source-control harness; v900 measured word-keys-exact harness from published release provenance. Frozen executable toolchains can differ from the current toolchain.',
 'limits':'Full-script input provides future events; not a streaming-packet model or actual dropped frames. No published x64 v900 package existed. Single CPU and priority verified externally for legacy executables; modern benchmark instrumentation is disabled to preserve common timing scope.'
},'builds':{name:{'path':str(exe.relative_to(ROOT)).replace('\\','/'),'sha256':hashlib.sha256(exe.read_bytes()).hexdigest()} for name,exe in builds.items()},'runs':[]}
gold,_=load(ROOT/'benchmark-results/source-control/render.csv')
fields=('frame','time_100ns','bitmaps','sha256')
pixels=[tuple(row[x] for x in fields) for row in gold]
for round_index,order in enumerate([list(builds),list(reversed(builds))],1):
 for name in order:
  stem=f'{round_index:02d}-{name}';output=BASE/(stem+'.csv')
  assert not output.exists()
  args=[str(builds[name])]
  if name!='original-Win32':args+=['--bitmap-cache-items','64']
  args += [str(ROOT/'benchmark-results/media/default.ass'),str(ROOT/'benchmark-results/media'),str(output)]
  print('Running',stem,flush=True)
  started=time.perf_counter()
  with (BASE/(stem+'.log')).open('w',encoding='utf-8') as log:
   child=subprocess.Popen(args,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,creationflags=0x8000)
   handle=ctypes.c_void_p(int(child._handle));allowed=ctypes.c_size_t();system=ctypes.c_size_t()
   assert k.GetProcessAffinityMask(handle,ctypes.byref(allowed),ctypes.byref(system)) and allowed.value==0x20
   assert k.GetPriorityClass(handle)==0x8000
   if child.wait():raise RuntimeError(stem+' failed')
  rows,metrics=load(output)
  actual=[tuple(row[x] for x in fields) for row in rows]
  assert actual==pixels,stem+' changed pixels'
  times=[float(row['render_ms']) for row in rows]
  result['runs'].append({'name':name,'round':round_index,'affinity_verified':'0x20','priority_verified':'AboveNormal','pixel_mismatches':0,'process_wall_seconds':time.perf_counter()-started,'render_total_ms':sum(times),'metrics':metrics,'spike_ms':{row['frame']:float(row['render_ms']) for row in rows if int(row['frame']) in [32249,32693,32780,32854,33157,33448,33973]}})
  (BASE/'comparison.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
  print(stem,'mean',round(metrics['render_ms_mean'],3),'p99',round(metrics['render_ms_p99'],3),'ms; all 2428 pixels identical',flush=True)
result['medians']={name:{key:statistics.median(run['metrics'][key] for run in result['runs'] if run['name']==name) for key in result['runs'][0]['metrics']} for name in builds}
result['comparisons']={}
for baseline in ['original-Win32','v900-Win32']:
 a=result['medians'][baseline];b=result['medians']['v910-Win32']
 result['comparisons'][baseline]={'mean_speedup':a['render_ms_mean']/b['render_ms_mean'],'mean_reduction_percent':100*(1-b['render_ms_mean']/a['render_ms_mean']),'p99_reduction_percent':100*(1-b['render_ms_p99']/a['render_ms_p99'])}
(BASE/'comparison.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'medians':result['medians'],'comparisons':result['comparisons']},indent=2),flush=True)
