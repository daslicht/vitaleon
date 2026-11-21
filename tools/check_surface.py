"""Structural and binding checks; the native MPC renderer is not emulated."""
from surface_states import *
from PIL import Image
skin=PROJECT/'skins'
assert len(UI['tabs'])==(6 if GROUP_MODEL else 1)
if GROUP_MODEL:assert [t['tabName'] for t in UI['tabs']]==['HOME','OSC','FILTER','ENV','MOD','FX'],'tab bar'
assert len(VIEWS)==40 and [sum(v['group']==g for v in VIEWS) for g in range(7)]==[4,4,2,6,8,10,6]
assert all(len(v['sections'])<=32 and all(len(s['items'])<=16 for s in v['sections']) for v in VIEWS)
replacement={x['key']:x['value'] for x in UI['replacementStrings']}
assert len(replacement['Parameter '+str(NAMES['Target'])])==429
assert len(replacement['Parameter '+str(NAMES['Parameter'])])==770
assert 'Parameter '+str(NAMES['Value']) not in replacement
assert replacement['Parameter '+str(NAMES['voice override'])]==['Kill','Steal']
assert len(replacement['Parameter '+str(NAMES['Table Step'])])==512
mapping=json.loads((skin/('Q-Links - Force.json' if GROUP_MODEL else 'Q-Links.json')).read_text())['Screen Mode Q-Links']['map']
if GROUP_MODEL:
    # One Q-Link map per tab, each bound to its group slots, so a knob names
    # the group it belongs to instead of a shared page model.
    assert len(mapping)==6 and all(m['Bank Direction']=='RowTopDown' for m in mapping)
    for g,m in enumerate(mapping):
        assert m['Tab']==g+1
        for slot in range(8):assert m['Q-Links'][f'Q-Link {slot+1}']==NAMES[f'Group {g} Slot {slot+1}'],(g,slot)
else:
    assert len(mapping)==1 and mapping[0]['Bank Direction']=='RowTopDown'
    for slot in range(16):
        row,col=divmod(slot,4);assert mapping[0]['Q-Links'][f'Q-Link {13+col-row*4}']==NAMES[f'UI Control {slot+1}']
image_cache={}
def image_size(name):
    if name not in image_cache:image_cache[name]=Image.open(skin/name).size
    return image_cache[name]
def parameter(h,mapping):
    h=resolve(h,mapping);m=re.search(r'Parameter (\d+)',h)
    assert m,('Unresolved native handle',h)
    n=int(m[1]);assert 0<=n<len(PARAMS) and n!=NAMES['Full Engine'];return n
checked=0
for page,section,values in states():
    leaves=expand(tab_entries(page),values);checked+=1
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
            p=parameter(d['handleName'],e['mapping'])
            if p==NAMES['UI Page']:assert d['numButtonsInGroup']==40 and 0<=d['buttonId']<40
            if p==NAMES['UI Group']:assert d['numButtonsInGroup']==7
        if kind=='Knob':
            assert image_size(d['filmStrip'])==(w,h*d['numFrames'])
            assert (w,h) in [(112,112),(280,56),(56,112),(120,52),(150,52),(300,56),(120,120),(372,64),(56,56),(64,64)],(e['name'],w,h)
    if VIEWS[page]['group']==1 and isinstance(VIEWS[page]['chart'],int) and section==0:
        segments=[e for e in leaves if e['name'].startswith('wave column')]
        left=min(e['box'][0] for e in segments);right=max(e['box'][2] for e in segments)
        top=min(e['box'][1] for e in segments);bottom=max(e['box'][3] for e in segments)
        assert (right-left)*9==(bottom-top)*16 and right-left==384,'Oscillator graph must be exactly 16:9'
    if page==24 and section==0:
        assert len([e for e in leaves if e['name']=='Effect switch'])==9,'FX overview must expose all nine effects'
result=audit();assert not result['errors'],result['errors']
print(f'PASS: 40 module views / {checked} states, seven footer groups, all nine FX, exact 16:9 oscillator graphs, native-size controls, complete bindings, Q-Link map, no control under the Q-Link sidebar and no label/control collisions')
