#!/usr/bin/env python3
"""Check live DS clip playback/contact timing and retain submitted PCM evidence."""
from pathlib import Path
import array,json,math,re,sys,wave
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/movement-lab'
instance=Path(sys.argv[1]) if len(sys.argv)>1 else Path((OUT/'latest-attempt.txt').read_text())
sd=instance/'user/sdmc/3ds/sm64ds';log=(sd/'movement-lab.log').read_text()
clips={};errors=[];voices={};previous={};actual_voices={};last_voice=None
for line in log.splitlines():
 if line.startswith('MOVEMENT_PRESENT '):
  tick,char,anim,flags,before,rate,length,contact,after=map(int,line.split()[1:])
  if length<=0:errors.append([tick,'empty animation']);continue
  expected=(before+rate)%length if not flags else min(length-1,max(0,before+rate))
  if after!=expected:errors.append([tick,'playback cursor',after,expected])
  key=(char,anim);row=clips.setdefault(key,dict(ticks=0,wraps=0,contacts=0,rates=set()))
  row['ticks']+=1;row['wraps']+=int(after<before and rate>0);row['contacts']+=contact;row['rates'].add(rate)
  if anim in (0x48,0x3f):
   if flags:errors.append([tick,'gait must loop',hex(anim),flags])
   # Check contacts against the actual loaded clip, before it advances.
   next_frame=before+rate
   feet=[4*4096,19*4096]
   hit=any(before<=f<next_frame for f in feet) if next_frame<length else any(before<=f<length or f<next_frame%length for f in feet)
   if contact!=hit:errors.append([tick,'foot contact mismatch'])
   if key in previous and previous[key][0]==tick-1 and before!=previous[key][1]:errors.append([tick,'same clip restarted'])
   previous[key]=(tick,after)
 elif line.startswith('MOVEMENT_DS_VOICE '):
  m=re.search(r'char=(\d+) voice=(\d+) handle=(\d+)',line)
  last_voice=tuple(map(int,m.groups()))
  actual_voices.setdefault(last_voice[0],set()).add(last_voice[1])
 elif line.startswith('MOVEMENT_LAB voice '):
  m=re.search(r'group=(-?\d+) voice=(-?\d+) char=(\d+)',line);group,voice,char=map(int,m.groups());voices[char]=voices.get(char,0)+1
  allowed=(group*2,group*2+1) if group>=0 else (voice,)
  if not last_voice or last_voice[0]!=char or last_voice[1] not in allowed or last_voice[2]==0:errors.append([char,'original DS voice API mismatch',last_voice,list(allowed)])
for char in range(4):
 for anim in (0x48,0x3f):
  row=clips.get((char,anim),{})
  if row.get('wraps',0)<2:errors.append([char,hex(anim),'fewer than two complete cycles'])
  if row.get('contacts',0)<4:errors.append([char,hex(anim),'insufficient foot contacts'])
 if voices.get(char,0)<2:errors.append([char,'insufficient voice events'])
pcm=(sd/'movement-lab.pcm').read_bytes();samples=array.array('h');samples.frombytes(pcm)
if sys.byteorder!='little':samples.byteswap()
peak=max(map(abs,samples),default=0);nonzero=sum(x!=0 for x in samples)
if not nonzero:errors.append(['silent submitted PCM'])
with wave.open(str(instance/'submitted-audio.wav'),'wb') as w:
 w.setparams((2,2,32728,0,'NONE','not compressed'));w.writeframes(pcm)
report={'status':'passed' if not errors else 'failed','errors':errors,'clips':{f'{char}:{anim:02x}':{**r,'rates':sorted(r['rates'])} for (char,anim),r in clips.items()},'voices':voices,'actual_ds_voice_ids':{c:sorted(v) for c,v in actual_voices.items()},'submitted_pcm':{'bytes':len(pcm),'duration_seconds':len(pcm)/4/32728.498,'peak':peak,'nonzero_samples':nonzero,'rms':math.sqrt(sum(x*x for x in samples)/max(1,len(samples)))},'scope':'Actual DS clip cursor, flags, rate and contact events; PCM tapped at NDSP submission. Nonzero PCM does not prove audible fidelity or physical-console acceptance.'}
(instance/'presentation-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2));sys.exit(bool(errors))
