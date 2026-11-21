"""One navigation/parameter model shared by native UI generation and Q-Link DSP.

Rule (user request 2026-11-13): every sound parameter appears exactly once.
Intended exceptions only: the HOME > PLAY shortcuts and the HOME filter button,
the FX overview switches, the wave-frame slider under each oscillator graph,
and the curve point editor shared by LFO and modulation-route curves.
tools/check_duplicates.py enforces this."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parent
CAT=json.loads((ROOT/'catalog.json').read_text())['controls']
GROUPS=['HOME','OSC','FILTER','ENV','LFO','FX','OTHER']
VIEWS=[]
def add(group,title,tab,sections,color='osc',chart=None,member=0,hidden=False):
    VIEWS.append(dict(group=group,title=title,tab=tab,sections=[dict(title=name,items=items) for name,items in sections],color=color,chart=chart,member=member,hidden=hidden))
# Modulation targets grouped for the two-step picker (group page -> entry page).
import re as _re
DESTINATIONS=json.loads((ROOT/'catalog.json').read_text())['destinations']
def _target_groups():
    def where(prefixes):return [d for d in DESTINATIONS if d.startswith(tuple(prefixes))]
    groups=[('OSC 1',where(['osc_1_'])),('OSC 2',where(['osc_2_'])),('OSC 3',where(['osc_3_'])),('SAMPLE',where(['sample_'])),
        ('FILTER 1',where(['filter_1_'])),('FILTER 2',where(['filter_2_'])),('FX FILTER',where(['filter_fx_'])),
        ('ENV 1-3',where(['env_1_','env_2_','env_3_'])),('ENV 4-6',where(['env_4_','env_5_','env_6_'])),
        ('LFO 1-2',where(['lfo_1_','lfo_2_'])),('LFO 3-4',where(['lfo_3_','lfo_4_'])),('LFO 5-6',where(['lfo_5_','lfo_6_'])),('LFO 7-8',where(['lfo_7_','lfo_8_'])),
        ('RANDOM',where(['random_'])),('CHORUS',where(['chorus_'])),('COMP',where(['compressor_'])),('DELAY',where(['delay_'])),('DISTORT',where(['distortion_'])),
        ('EQ',where(['eq_'])),('FLANGER',where(['flanger_'])),('PHASER',where(['phaser_'])),('REVERB',where(['reverb_']))]
    number=lambda d:int(_re.match(r'modulation_(\d+)_',d)[1])
    mods=sorted(where(['modulation_']),key=lambda d:(number(d),d))
    for first in range(1,65,16):groups.append((f'MOD {first}-{first+15}',[d for d in mods if first<=number(d)<first+16]))
    used={d for _,items in groups for d in items}
    groups.insert(0,('GLOBAL',[d for d in DESTINATIONS if d not in used]))
    assert sum(len(items) for _,items in groups)==len(DESTINATIONS) and all(0<len(items)<=32 for _,items in groups),[(n,len(i)) for n,i in groups]
    return groups
TARGET_GROUPS=_target_groups()
def keys(prefix):return [c['id'] for c in CAT if c['id'].startswith(prefix)]
def sections(items,name='DETAILS',size=6,first='SOUND'):
    # Three wide columns hold six controls in two rows (0.3.25), so every
    # section is chunked to that size: FIRST, NAME, NAME 2, ...
    result=[(first,items[:size])]
    for n in range(size,len(items),size):
        part=n//size
        result.append((name if part==1 else name+' '+str(part),items[n:n+size]))
    return result
add(0,'Home','HOME',[('PLAY',['Home Master','Voices','Home Cutoff','Home Resonance','Home Attack','Home Release']),('PLAY 2',['Home Unison','Home Detune']),('MACROS',['macro_control_1','macro_control_2','macro_control_3','macro_control_4'])],chart='home')
add(0,'Voices','VOICES',[('PLAY',['Voices','voice_priority','voice_override','legato','portamento_time','portamento_force']),('PLAY 2',['portamento_scale','portamento_slope']),('EXPRESSION',['velocity_track','pitch_bend_range','mpe_enabled','Lower MPE Members','Upper MPE Members','oversampling']),('EXPRESSION 2',['stereo_mode','stereo_routing'])],'env')
add(0,'Advanced','ADVANCED',[('PARAMETERS',['Parameter','Value','Live Graphics'])])
add(0,'Library','LIBRARY',[('BANKS',[])],chart='library')
for o in range(1,4):
    # Medium 16:9 graph top-left with four controls around it, six controls in
    # the other sections (0.3.25: three wide columns, two rows).
    p=f'osc_{o}_';parts=[('SOUND',['wave_frame','level','on']),
        ('PHASE',['phase','random_phase','transpose_quantize','midi_track','smooth_interpolation','destination']),
        ('UNISON',['unison_voices','unison_detune','unison_blend','stereo_spread','stack_style','detune_power']),
        ('SPREAD',['pan','transpose','tune','detune_range','frame_spread','spectral_unison']),
        ('WARP',['distortion_type','distortion_amount','distortion_spread','distortion_phase']),
        ('SPECTRAL',['spectral_morph_type','spectral_morph_amount','spectral_morph_spread'])]
    parts=[(name,[k if k.startswith('Osc ') else p+k for k in items]) for name,items in parts]
    # 0.3.34: the table list moved from its own section into SOUND, right of the
    # wave frame knob (drawn by generate_surface.add_table). The four handles that
    # section used as items (Open Osc N Table, Table Cursor/Oscillator/Page) are
    # UI state only now: SOUND is the oscillator's page, so nothing has to open a
    # separate table page and no Q-Link slot is wasted on an invisible control.
    assert set(keys(p))-{p+'view_2d'}-{f'Osc {o} Table','Table Cursor','Table Oscillator','Table Page'}=={k for _,items in parts for k in items}
    add(1,f'Oscillator {o}',f'OSC {o}',parts,chart=o-1,member=o)
add(1,'Sample','SAMPLE',[('PLAY',['Sample File','sample_on','sample_level','sample_pan','sample_transpose','sample_tune']),('PLAY 2',['sample_loop','sample_bounce']),('ROUTING',['sample_keytrack','sample_random_phase','sample_destination','sample_transpose_quantize'])])
for f in range(1,3):
    p=f'filter_{f}_';main=[p+k for k in ['cutoff','resonance','drive','mix','on','model','style','blend']]
    add(2,f'Filter {f}',f'FILTER {f}',sections(main+[k for k in keys(p) if k not in main],'ROUTING'),'fx')
for e in range(1,7):add(3,f'Envelope {e}',f'ENV {e}',[('ADSR',['Env attack','Env decay','Env sustain']),('TIME',['Env release','Env delay','Env hold']),('CURVE',['Env attack_power','Env decay_power','Env release_power'])],'env','env',e)
for l in range(1,9):add(4,f'LFO {l}',f'LFO {l}',[('TIMING',['LFO frequency','LFO sync','LFO tempo']),('SYNC',['LFO sync_type','LFO phase','LFO stereo']),('DELAY',['LFO delay_time','LFO fade_time','LFO Shape']),('SHAPE',['LFO File','Smooth Curve','Point Count']),('POINT',['Point','Point Time','Point Level']),('POINT 2',['Point Curve','LFO smooth_mode','LFO smooth_time']),('KEYTRACK',['LFO keytrack_transpose','LFO keytrack_tune'])],'mod','lfo',l)
effects=['distortion','chorus','flanger','phaser','delay','reverb','compressor','eq','filter_fx']
add(5,'Effects overview','OVERVIEW',[('EFFECTS',[e+'_on' for e in effects]),('ORDER',['FX Position','FX At Position'])],'fx',chart='effects')
for effect in effects:
    titles={'filter_fx':'FX FILTER','compressor':'COMP','distortion':'DISTORT'}
    k=keys(effect+'_');priority=[effect+'_'+x for x in ['on','dry_wet','mix','type','drive','frequency','feedback','decay_time','size','cutoff','resonance'] if effect+'_'+x in k]
    ordered=priority+[x for x in k if x not in priority]
    add(5,effect.replace('_',' ').title(),titles.get(effect,effect.upper()),[(['MAIN','TONE','BANDS','MORE'][min(n//6,3)],ordered[n:n+6]) for n in range(0,len(ordered),6)],'fx')
# Serum-style table (user request): eight routes per page, source - amount - target.
# The Q-Links of a page are the eight amounts.
add(6,'Matrix','MATRIX',[(f'{page*8+1}-{page*8+8}',[f'modulation_{route}_amount' for route in range(page*8+1,page*8+9)]) for page in range(8)],'mod','mxtable',1)
add(6,'Random','RANDOM',[('TIMING',['Random Select']+['Random '+k for k in ['frequency','sync','tempo','style','stereo']]),('TUNING',['Random sync_type','Random keytrack_transpose','Random keytrack_tune'])],'mod')
add(6,'Expression','EXPRESSION',[('PERFORM',['mod_wheel','pitch_wheel','voice_amplitude'])],'mod')
# No separate routing page: each of its controls already lives in its own
# module (OSC > PHASE, SAMPLE > ROUTING, FILTER, FX FILTER, VOICES).
add(6,'Modulation curve','MOD CURVE',[('CURVE',['Route Select','Curve','Point Count']),('POINT',['Point','Point Time','Point Level']),('POINT 2',['Point Curve','Smooth Curve'])],'mod','curve',1)
# Hidden helper views of the target picker; reached only through the matrix.
add(6,'Target group','',[('GROUPS',[])],'mod','pickgroup',hidden=True)
add(6,'Target','',[('-',[])]+[(name,[]) for name,_ in TARGET_GROUPS],'mod','picktarget',hidden=True)
def write_header():
    payload=json.dumps(VIEWS,separators=(',',':'))
    (ROOT.parent/'src/SurfaceMap.h').write_text('// SPDX-License-Identifier: GPL-3.0-or-later\n// Generated by tools/surface_spec.py; do not edit.\n#pragma once\nstatic const char* surfaceMapJson=R"SURFACE('+payload+')SURFACE";\n',encoding='utf-8')
if __name__=='__main__':write_header();print(len(VIEWS),'module views across',len(GROUPS),'groups')
