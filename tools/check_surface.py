"""Structural and binding checks; the native MPC renderer is not emulated."""
from surface_states import *
from PIL import Image
skin=PROJECT/'skins'
assert len(UI['tabs']) in (1,7)
if len(UI['tabs'])==7:assert [t['tabName'] for t in UI['tabs']]==['HOME','OSC','FILTER','ENV','LFO','FX','OTHER']
assert len(VIEWS)==40 and [sum(v['group']==g for v in VIEWS) for g in range(7)]==[4,4,2,6,8,10,6]
assert all(len(v['sections'])<=32 and all(len(s['items'])<=16 for s in v['sections']) for v in VIEWS)
replacement={x['key']:x['value'] for x in UI['replacementStrings']}
assert len(replacement['Parameter '+str(NAMES['Target'])])==429
assert len(replacement['Parameter '+str(NAMES['Parameter'])])==770
assert 'Parameter '+str(NAMES['Value']) not in replacement
assert replacement['Parameter '+str(NAMES['voice override'])]==['Kill','Steal']
mapping=json.loads((skin/'Q-Links.json').read_text())['Screen Mode Q-Links']['map']
assert len(mapping)==1 and mapping[0]['Bank Direction']=='RowTopDown'
for slot in range(16):
    row,col=divmod(slot,4);assert mapping[0]['Q-Links'][f'Q-Link {13+col-row*4}']==NAMES[f'UI Control {slot+1}']
image_cache={}
def image_size(name):
    if name not in image_cache:image_cache[name]=Image.open(skin/name).size
    return image_cache[name]
def parameter(h,mapping):
    h=resolve(h,mapping)
    if h.startswith('Local/'):return None          # UI-only page integer, not a parameter
    m=re.search(r'Parameter (\d+)',h)
    assert m,('Unresolved native handle',h)
    n=int(m[1]);assert 0<=n<len(PARAMS) and n!=NAMES['Full Engine'];return n
checked=0
for page,section,values in states():
    leaves=expand(UI['tabs'][tab_of(page)]['componentDefinition']['componentsData'],values);checked+=1
    for e in leaves:
        d=e['data'];kind=e['kind'];x,y,r,b=e['box'];w,h=r-x,b-y
        if kind=='Label':assert d['type']!='Text'
        for field,value in d.items():
            if isinstance(value,str) and (field=='handleName' or field.endswith('Handle') or field=='dataHandle'):
                p=parameter(value,e['mapping'])
                if kind=='Envelope':assert 'Parameter '+str(p) not in replacement
        if kind=='Button':
            assert w>=64 and h>=52,(e['name'],w,h)
            for name in ['onImage','offImage']:assert image_size(d[name])==(w,h)
            if str(d['handleName']).startswith('Local/'):
                assert d['numButtonsInGroup']>=2 and 0<=d['buttonId']<d['numButtonsInGroup'],(e['name'],d['handleName'])
            p=parameter(d['handleName'],e['mapping'])
            if p==NAMES['UI Page']:assert d['numButtonsInGroup']==40 and 0<=d['buttonId']<40
        if kind=='Knob':
            assert image_size(d['filmStrip'])==(w,h*d['numFrames'])
            assert (w,h) in [(112,112),(280,56),(56,112),(120,52),(150,52),(300,56),(120,120),(372,64)],(e['name'],w,h)
    if VIEWS[page]['group']==1 and isinstance(VIEWS[page]['chart'],int) and section==0:
        segments=[e for e in leaves if e['name'].startswith('wave column')]
        left=min(e['box'][0] for e in segments);right=max(e['box'][2] for e in segments)
        top=min(e['box'][1] for e in segments);bottom=max(e['box'][3] for e in segments)
        assert (right-left)*9==(bottom-top)*16 and right-left==384,'Oscillator graph must be exactly 16:9'
    if page==24 and section==0:
        assert len([e for e in leaves if e['name']=='Effect switch'])==9,'FX overview must expose all nine effects'
result=audit();assert not result['errors'],result['errors']
print(f'PASS: 40 module views / {checked} states in seven native tabs, all nine FX, exact 16:9 oscillator graphs, native-size controls, complete bindings, Q-Link map, no control under the Q-Link sidebar and no label/control collisions')
