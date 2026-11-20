"""Regression for the 0.3.0 blank body: validate parent models, not just leaves."""
from surface_states import *
import copy

def invalid_parents(entries,mapping=None,path='root'):
    mapping=mapping or {};errors=[]
    for e in entries:
        d=e['componentData'];local=dict(mapping)
        for p in e['handle remapping']['map']:local[p['key']]=resolve(p['value'],mapping)
        if d['type'] not in DEFS:continue
        name=path+'/'+d['name']
        if not bound(d['data']['handleName'],local):errors.append(name)
        errors+=invalid_parents(DEFS[d['type']]['componentsData'],local,name)
    return errors

entries=UI['tabs'][0]['componentDefinition']['componentsData']
assert not invalid_parents(entries),invalid_parents(entries)
# Remove exactly the bindings absent in 0.3.0. The same visibility model must
# now reject every content view even though its individual controls are bound.
broken=copy.deepcopy(entries)
for e in broken:
    if e['componentData']['type'].startswith('view-'):e['handle remapping']['map']=[]
assert len(invalid_parents(broken))==28
for page,section,values in states():
    old=expand(broken,values);fixed=expand(entries,values)
    content=lambda items:[e for e in items if e['kind']=='Label' and 64<=e['box'][1]<568]
    assert not content(old),(page,section,'old regression must reproduce a blank body')
    assert content(fixed),(page,section,'fixed body must contain real controls/text')
    if VIEWS[page]['chart']!='library' and not (VIEWS[page]['chart']=='picktarget' and section==0):  # section 0 of the picker is never shown
        assert any((e['kind'] in ['Knob','Button','Slider'] or (e['kind']=='Label' and e['data']['type']=='Value')) and 128<=e['box'][1]<568 for e in fixed),(page,section,'control/value fields missing')
states_checked=len(list(states()))
print(f'PASS: all 28 parent models bound; original defect reproduced in all {states_checked} states; fixed content present in all {states_checked} states')
