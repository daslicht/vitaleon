"""Every radio group, IndexedEnabling condition and replacementStrings entry that
resolves to one parameter must agree on its step count (SDK ui-reference 7.4).
Static consistency check of the generated skin; the MPC renderer is not emulated."""
import json,re,collections
from pathlib import Path
project=Path(__file__).resolve().parent.parent
page=json.loads((project/'skins/TUI.json').read_text(encoding='utf-8'))['pageData']
params=json.loads((project/'build/params.json').read_text())['params']
strings={d['key']:d['value'] for d in page['replacementStrings']}
definitions={d['key']:d['value'] for d in page['componentDefinitions']['localComponentDefinitions']}
uses=collections.defaultdict(set)
def resolve(handle,chain):
    for mapping in chain:
        for key,value in mapping.items():
            if handle==key:handle=value
            elif handle.endswith('/'+key):handle=handle[:-len(key)]+value
    return handle
def target(handle,chain):
    handle=resolve(handle,chain);m=re.search(r'Parameter (\d+)$',handle)
    assert m,('Unresolved handle',handle);return int(m[1])
def walk(component,chain):
    for entry in component['componentsData']:
        inner=[{x['key']:x['value'] for x in entry['handle remapping']['map']}]+chain
        model=entry['componentData']
        for handle in entry['bounds'].get('additionalInvalidatingHandles',[]):
            for m in re.finditer(r'IndexedEnabling/(\d+)/(\d+)/',handle):
                assert int(m[1])<int(m[2]),handle
                uses[target(handle,inner)].add(('IndexedEnabling',int(m[2])))
        if model['type']=='Button':
            data=model['data'];assert 0<=data['buttonId']<max(data['numButtonsInGroup'],2),data
            uses[target(data['handleName'],inner)].add(('Button',max(data['numButtonsInGroup'],2)))
        if model['type'] in definitions:walk(definitions[model['type']],inner)
for tab in page['tabs']:walk(tab['componentDefinition'],[])
errors=[]
for index,found in sorted(uses.items()):
    counts={n for _,n in found};key='Parameter '+str(index)
    if key in strings:counts.add(len(strings[key]))
    if len(counts)!=1:errors.append((index,params[index]['name'],sorted(found),len(strings[key]) if key in strings else None))
for name in ['UI Group','UI Page','UI Section','UI Pick']:
    index=next(p['index'] for p in params if p['name']==name)
    if 'Parameter '+str(index) in strings:errors.append((index,name,'navigation parameter has replacementStrings'))
# Overlay widgets need their two "version" keys; a JSON round trip drops one.
raw=(project/'skins/TUI.json').read_text(encoding='utf-8')
overlays=0
for m in re.finditer(r'"type": "(KnobOverlay|MenuOverlay)",\s*"data": \{\s*"version": (\d),\s*"(\w+)": (\d)?',raw):
    overlays+=1;expected=('2','1') if m[1]=='KnobOverlay' else ('1','1')
    if m[3]!='version' or (m[2],m[4])!=expected:errors.append((m[1],'needs version keys',expected))
if overlays!=2:errors.append(('expected 2 overlay widgets',overlays))
assert not errors,errors
print(f'PASS: {len(uses)} button/visibility parameters have one consistent step count')
