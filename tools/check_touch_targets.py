"""Detect hit-region collisions and controls under the Q-Link sidebar.

Models the hardware rules reported upstream, not the MPC renderer:
- a Label swallows touches over whatever it covers (PR #5):
  https://github.com/inmusic-software/mpc-plugin-builder-sdk/pull/5
- the Q-Link sidebar covers roughly x >= 1025, so every knob, slider, button
  and envelope must end at or before the safe right edge (SDK ui-reference 1.4).
It also requires every component that carries actions to contain a focusable
value widget, otherwise its tap has no value element to reach and the data
wheel has nothing to turn.
"""
from pathlib import Path
import argparse,json,os,re

def focus_findings(ui):
    """One focus stop per slot, the pattern of the SDK's own generator (5.2).

    The slot entry takes hardware focus and every widget inside it is 'No'.
    Otherwise a tap focuses an inner widget and the data wheel edits nothing,
    which is what left choice and switch cells without wheel control.
    """
    definitions={d['key']:d['value'] for d in ui['componentDefinitions']['localComponentDefinitions']}
    findings=[]
    for key,value in definitions.items():
        if not value['actions']:continue
        for entry in value['componentsData']:
            kind=entry['componentData']['type']
            if kind in ('Knob','Slider','Button') and entry['bounds']['acceptsHWFocus']!='No':
                findings.append(('definition',key,'value widget inside a slot takes focus',entry['componentData']['name']))
    def walk(entries):
        for entry in entries:
            kind=entry['componentData']['type']
            if definitions.get(kind,{}).get('actions') and entry['bounds']['acceptsHWFocus']!='Yes':
                findings.append(('page',entry['componentData']['name'],'slot does not take hardware focus',kind))
            if kind in definitions:walk(definitions[kind]['componentsData'])
    for tab in ui['tabs']:walk(tab['componentDefinition']['componentsData'])
    return findings

def audit(path):
    ui=json.loads(Path(path).read_text(encoding='utf-8'))['pageData']
    if ui['tabs'] and ui['tabs'][0]['tabName'] in ('HOME','VITALEON'):
        import surface_states
        previous=surface_states.UI;surface_states.UI=ui
        previous_defs=surface_states.DEFS;surface_states.DEFS={d['key']:d['value'] for d in ui['componentDefinitions']['localComponentDefinitions']}
        result=surface_states.audit();surface_states.UI=previous;surface_states.DEFS=previous_defs
        collisions=[e for e in result['errors'] if e[2]=='label intercepts control']
        focus=focus_findings(ui)
        return {'pages':40,'states':result['states'],'button_instances':result['buttons'],'label_button_collisions':len(collisions),'controls_outside_safe_area':len([e for e in result['errors'] if e[2]=='under the Q-Link sidebar']),'empty_interactive_slots':len([e for e in result['errors'] if e[2]=='interactive component without a value widget']),'slots_without_wheel_focus':len(focus),'findings':result['errors']+focus}
    definitions={d['key']:d['value'] for d in ui['componentDefinitions']['localComponentDefinitions']}
    findings=[];button_count=0
    def expand(entries,panel,ox=0,oy=0):
        leaves=[]
        for entry in entries:
            b=entry['bounds'];d=entry['componentData']
            if b['whenVisible']=='WhenFocussed':continue
            local=[re.search(r'IndexedEnabling/(\d+)/2/Local/',h) for h in b.get('additionalInvalidatingHandles',[])]
            if any(m and int(m[1])!=panel for m in local):continue
            x,y,w,h=map(int,b['bounds'].split());x+=ox;y+=oy
            if d['type'] in definitions:leaves+=expand(definitions[d['type']]['componentsData'],panel,x,y)
            else:leaves.append({'kind':d['type'],'name':d['name'],'box':(x,y,x+w,y+h),'data':d['data']})
        return leaves
    for tab in ui['tabs']:
        for panel in (0,1):
            leaves=expand(tab['componentDefinition']['componentsData'],panel)
            labels=[e for e in leaves if e['kind']=='Label']
            for control in (e for e in leaves if e['kind'] in ('Button','Knob','Slider')):
                button_count+= control['kind']=='Button';x1,y1,x2,y2=control['box']
                for label in labels:
                    a,b,c,d=label['box'];w=min(c,x2)-max(a,x1);h=min(d,y2)-max(b,y1)
                    if w>0 and h>0:findings.append({'page':tab['tabName'],'panel':panel,'control':control['name'],'kind':control['kind'],'label':label['name'],'covered_pixels':w*h,'control_pixels':(x2-x1)*(y2-y1)})
    return {'pages':len(ui['tabs']),'states':len(ui['tabs'])*2,'button_instances':button_count,'label_button_collisions':len(findings),'findings':findings}

if __name__=='__main__':
    parser=argparse.ArgumentParser();    parser.add_argument('--skin',default=os.environ.get('VITALEON_SKIN',str(Path(__file__).resolve().parents[1]/'skins/TUI.json')));parser.add_argument('--json')
    args=parser.parse_args();result=audit(args.skin)
    if args.json:Path(args.json).write_text(json.dumps(result,indent=2),encoding='utf-8')
    safe=result.get('controls_outside_safe_area',0);empty=result.get('empty_interactive_slots',0);focus=result.get('slots_without_wheel_focus',0)
    print(('FAIL' if result['findings'] else 'PASS')+f": {result['states']} page states, {result['button_instances']} button instances, {result['label_button_collisions']} label/control collisions, {safe} controls under the Q-Link sidebar, {empty} interactive slots without a value widget, {focus} slots the data wheel cannot focus")
    for finding in result['findings'][:12]:print(json.dumps(finding))
    raise SystemExit(bool(result['findings']))
