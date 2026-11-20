"""Generate native MPC skins and a source-derived layout preview, no web runtime."""
from pathlib import Path
import json,sys,math,re,hashlib,os
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parent; project=root.parent; sdk=project.parents[1]
sys.path.insert(0,str(sdk/'tools'));import gen_ui as g
g.FONT_NAME='Roboto'
# Live II hardware drops unbound Text labels, while bound Name labels render.
# Use the proven WithName path for every fixed caption, including nested modes.
sdk_label_data=g.label_data
def native_label_data(label_type,colour,height=30.0,justification='left',text=None):
    result=sdk_label_data('Name' if label_type=='Text' else label_type,colour,height,justification)
    if label_type=='Text':
        assert text is not None and ']' not in text
        return g.Obj([(key,'WithName['+text+']/Parameter 0' if key=='handleName' else value) for key,value in result])
    return result
g.label_data=native_label_data
skin=project/'skins';skin.mkdir(exist_ok=True)
params=json.loads((project/'build/params.json').read_text())['params']; byname={p['name']:p for p in params}
# The build's parameter dump holds the values of whatever sound the plugin had loaded
# when it was made (since 0.3.20 the first installed sound). Value and text of every
# parameter therefore come from tools/init-values.json: the Init state of a plugin
# without sounds, written by tests/run_full_checks.py. The skin no longer depends on
# the installed sounds.
init_values=json.loads((root/'init-values.json').read_text())
assert len(init_values)==len(params),'tools/init-values.json is stale: run tests/run_full_checks.py and copy build/full-checks/init-values.json'
for p,v in zip(params,init_values):
    assert v['index']==p['index'] and v['name']==p['name'],(p['name'],v['name'])
    p['default']=v['value'];p['display']=v['display']
catalog_data=json.loads((root/'catalog.json').read_text());catalog=catalog_data['controls']
catalog_by_id={c['id']:c for c in catalog}
# The host metadata dump omits very long choice lists. The pinned engine catalog
# still provides their complete, ordered labels for the native menu overlay.
def target_name(d):
    d=re.sub(r'^modulation_(\d+)_amount$',r'mod_\1_amount',d);d=re.sub(r'^modulation_(\d+)_power$',r'mod_\1_curve',d)
    d=d.replace('spectral_morph','spectral').replace('distortion_','warp_',1) if d.startswith('osc_') else d
    d=d.replace('keytrack_transpose','key_transp').replace('keytrack_tune','key_tune').replace('formant_','form_').replace('filter_fx','fx_filter').replace('macro_control','macro')
    return ' '.join(w.upper() if w in ('osc','lfo','env','fx','eq') else w.capitalize() for w in d.split('_'))
def source_name(s):
    if s in ('None','none'):return 'None'
    return ' '.join(w.upper() if w in ('lfo','env') else w.capitalize() for w in s.replace('macro_control','macro').split('_'))
for p in params:
    key=p['name'].replace(' ','_')
    for title,prefix in [('LFO ','lfo_1_'),('Random ','random_1_'),('Env ','env_1_')]:
        if p['name'].startswith(title):key=prefix+p['name'][len(title):]
    c=catalog_by_id.get(key)
    if c and c['scale']==0 and c['min']==0 and len(c.get('choices',[]))>1:
        p['choices']=c['choices'];p['kind']='bool' if c['choices']==['Off','On'] else 'choice'
    if p['name']=='Target' or re.fullmatch(r'Route \d+ Target',p['name']):
        p['choices']=['None']+[target_name(d) for d in catalog_data['destinations']];p['kind']='choice'
    if p['name']=='Source' or re.fullmatch(r'Route \d+ Source',p['name']):
        p['choices']=[source_name(c) for c in p['choices']]
    elif p['name']=='Parameter':
        p['choices']=[c['id'] for c in catalog if c['id'] not in ['polyphony','beats_per_minute']];p['kind']='choice'
    elif p['name']=='Value' or (p['name'].startswith('Home ') and p['name']!='Home Filter'):
        # This normalized proxy can edit any parameter; its initial Off display
        # must never turn it into an on/off switch in the generated skin.
        p['kind']='float';p['choices']=[]
    # Selectors whose entries come from the Content folder or the library at run time
    # (tables, samples, presets, library pages) must never get a fixed label list: the
    # parameter dump only shows what happened to be installed while it was made.
    if p['name'].startswith('Library ') or p['name']=='Preset' or 'Table' in p['name'] or (p['kind']!='bool' and 'File' in p['name']):
        if p['kind']=='bool' and p['name'].startswith('Library ') and p.get('choices')!=['Off','On']:p['kind']='int'  # a 2-entry list is not a switch
        if p.get('choices')!=['Off','On']:p['choices']=[]
    # The library command buttons set themselves back after a tap. The parameter dump
    # therefore reads them as switches or as plain values, depending on timing
    # (0.3.20 lost their labels that way). They are switches.
    if p['name'] in ['Drives','Up Folder','Refresh Drives','Previous Files','Next Files','Installed Banks']+['Open Row %d'%n for n in range(1,7)]:
        p['kind']='bool';p['choices']=['Off','On']
    if p['name'] in ['UI Group','UI Page','UI Section','UI Pick','UI Bank']:
        # Navigation state must never get replacementStrings. The parameter
        # dump sweeps UI Section while the 2-section Home view clamps it, so it
        # is misdetected as a 0/1 switch. A 2-string entry makes the MPC treat
        # it as 2 steps while its buttons and IndexedEnabling use 4 (0.3.1:
        # section buttons tappable, content never switched).
        p['kind']='int';p['choices']=[]
BG='ff181a23';PANEL='ff222531';LINE='ff353949';TEXT='ffedf0f7';MUTED='ffa0a7b7'
COLORS={'osc':'ffbca3ff','env':'ff64d9ba','mod':'fff6bf66','fx':'ff7cb9e8'}
# Hardware safe area (SDK ui-reference 1.4): the Q-Link sidebar covers the right
# edge, roughly x >= 1025, as soon as a Q-Link is touched, and stays. Three
# columns of 308 px at x = 24, 356, 688 end at x = 996, so every control stays
# out of the sidebar and each column stays wide enough to read and to hit.
# Two layouts from one design. The MPC keeps the 1000 px safe area above
# because its Q-Link sidebar covers x >= 1025. The Force has no sidebar, so its
# skin fills the screen: three 394 px columns at x 24, 442, 860 (right edge
# 1254), and its slots are drawn at their own size instead of being stretched.
FORCE=os.environ.get('VITALEON_VARIANT')=='force'
SLOT_W,SLOT_H=(394,156) if FORCE else (308,156)
SLOT=f'0 0 {SLOT_W} {SLOT_H}'
SCREEN_RIGHT=1256 if FORCE else 1000
KNOB=120 if FORCE else 112
FADER_W,FADER_H=(372,64) if FORCE else (300,56)
TOGGLE_W,TOGGLE_H=(168,88) if FORCE else (136,72)
SEG_W,SEG_H=(186,56) if FORCE else (148,52)
SEG_DX,SEG_DY=(194,62) if FORCE else (152,58)
SLOT_FONT=26 if FORCE else 22
DEF_SUFFIX='-f' if FORCE else ''
def full(v):return round(v*SCREEN_RIGHT/1000)
def col_x(col):return 24+(SLOT_W+24)*col
def handle(name):
    if name not in byname:
        if name.replace('_',' ') in byname:name=name.replace('_',' ')
        else:raise ValueError('Unknown parameter '+name)
    return 'Parameter '+str(byname[name]['index'])
def index(name):return int(handle(name).split()[-1])
def ent(name,kind,data,bounds,focus=False,remap=()):return g.entry(name,kind,data,' '.join(map(str,bounds)),'Yes' if focus else 'No','Show','Always',remap)

def conditional(entry,handles):
    bounds=dict(entry)['bounds'];data=dict(bounds)
    data['version']=2;data['showWhenDataModelInvalid']='Hide'
    data['additionalInvalidatingHandles']=data.get('additionalInvalidatingHandles',[])+handles
    bounds[:]=list(data.items())
    return entry
def label(text,bounds,color=TEXT,size=22,value=None):
    return ent(text,'Label',g.label_data('Value' if value else 'Text',color,size,'left verticallyCentred',None if value else text),bounds,remap=[('Data',handle(value))] if value else [])
def rect(name,bounds,color):return ent(name,'Image',{'version':1,'colour':color,'image':''},bounds)
def film(name,bounds,param,color,bank,bankparam):
    data={'version':2,'imageType':'FilmStrip','colour':'0','image':f'segment-{color}-{bank}.png','numFrames':256,'handleName':handle(param),'animated':False}
    return g.entry(name,'Image',data,' '.join(map(str,bounds)),'No','Hide','Always',invalidating=[f'IndexedEnabling/{bank}/4/'+handle(bankparam)])
def graph(children,base,x=32,y=104,w=1216,h=156,color='osc'):
    children.append(rect('graph background',(x-8,y-4,w+16,h+8),'ff1c1f29'))
    for n in range(5):children.append(rect('horizontal grid',(x,y+n*h//4,w,1),'ff303441'))
    for n in range(9):children.append(rect('vertical grid',(x+n*w//8,y,1,h),'ff292d39'))
    for i in range(96):
        a=round(x+i*w/96);b=round(x+(i+1)*w/96)
        bankbase=base.replace('display_wave_','display_wave_bank_').replace('display_lfo_curve_','display_lfo_bank_')
        for bank in range(4):children.append(film('curve segment '+str(i),(a,y,b-a,h),base+str(i),color,bank,bankbase+str(i)))
    if base=='display_lfo_curve_':
        data={'version':2,'imageType':'FilmStrip','colour':'0','image':'lfo-playhead.png','numFrames':512,'handleName':handle('display_lfo_phase'),'animated':False}
        children.append(g.entry('LFO playhead','Image',data,f'{x} {y} {w} {h}','No','Hide','Always',invalidating=['IndexedEnabling/0/2/'+handle('Curve Target')]))
GRAPH=dict((k,int(v)) for k,v in re.findall(r'(kWaveColumns|kWaveLevels|kCurveColumns|kCurveLevels)=(\d+)',(project/'src/FullSynth.h').read_text(encoding='utf-8')))
assert len(GRAPH)==4,GRAPH
def wave_bars(children,chart,x,y,w,h):
    # 0.3.18: 128 columns of 3 px and 108 levels (was 64 x 64, which looked blocky).
    # The number of columns no longer costs host notifications: the plugin asks
    # for ONE display update per picture (see notifyVisuals in FullRuntime.inc).
    columns,levels=GRAPH['kWaveColumns'],GRAPH['kWaveLevels'];assert w%columns==0;cw=w//columns
    name=f'wavebar-{cw}x{h}-{levels}.png'
    children.append(rect('graph background',(x-8,y-4,w+16,h+8),'ff1c1f29'))
    children.append(rect('zero line',(x,y+h//2,w,1),'ff353949'))
    for i in range(columns):
        param=f'display_wave_{chart}_{i}' if i<96 else f'display_wave_bank_{chart}_{i-96}'
        data={'version':2,'imageType':'FilmStrip','colour':'0','image':name,'numFrames':levels,'handleName':handle(param),'animated':False}
        children.append(g.entry('wave column '+str(i),'Image',data,f'{x+i*cw} {y} {cw} {h}','No','Show','Always'))
    if not (skin/name).exists():
        strip=Image.new('RGBA',(cw,h*levels));d=ImageDraw.Draw(strip);rgb=tuple(bytes.fromhex(COLORS['osc'][2:]))
        for k in range(levels):
            top=k*h;level=round((1-k/(levels-1))*(h-3));mid=h//2
            d.rectangle((0,top+min(level,mid),cw-1,top+max(level+2,mid)),fill=rgb+(70,))
            d.rectangle((0,top+level,cw-1,top+level+2),fill=rgb+(255,))
        strip.save(skin/name)
def curve_bars(children,x,y,w,h):
    # LFO / route curve: 192 columns of 6 px and 96 levels (was 96 x 64).
    columns,levels=GRAPH['kCurveColumns'],GRAPH['kCurveLevels'];assert w%columns==0;cw=w//columns
    children.append(rect('graph background',(x-8,y-4,w+16,h+8),'ff1c1f29'))
    name=f'curvebar-{cw}x{h}-{levels}.png'
    for i in range(columns):
        param=f'display_lfo_curve_{i}' if i<96 else f'display_lfo_bank_{i-96}'
        data={'version':2,'imageType':'FilmStrip','colour':'0','image':name,'numFrames':levels,'handleName':handle(param),'animated':False}
        children.append(g.entry('curve column '+str(i),'Image',data,f'{x+i*cw} {y} {cw} {h}','No','Show','Always'))
    data={'version':2,'imageType':'FilmStrip','colour':'0','image':'lfo-playhead.png','numFrames':512,'handleName':handle('display_lfo_phase'),'animated':False}
    children.append(g.entry('LFO playhead','Image',data,f'{x} {y} {w} {h}','No','Hide','Always',invalidating=['IndexedEnabling/0/2/'+handle('Curve Target')]))
    if not (skin/name).exists():
        strip=Image.new('RGBA',(cw,h*levels));d=ImageDraw.Draw(strip);rgb=tuple(bytes.fromhex(COLORS['mod'][2:]))
        for k in range(levels):
            top=k*h;level=round((1-k/(levels-1))*(h-3))
            d.rectangle((0,top+level,cw-1,top+h-1),fill=rgb+(55,))
            d.rectangle((0,top+level,cw-1,top+level+2),fill=rgb+(255,))
        strip.save(skin/name)
def trace(name,bounds,param,color):
    c=COLORS[color]
    return ent(name,'Plotter',{'version':2,'backgroundColour':'ff1c1f29','drawLine':True,'lineDirection':'Up','tightClip':True,'lineColourStart':c,'lineColourEnd':c,'horizLineGradient':False,'lineWidth':2.0,'fillColourStart':'1064d9ba','fillColourEnd':'0064d9ba','horizFillGradient':False,'fillZero':0.0,'dataHandle':handle(param),'drawHistory':True,'historyDirection':'Left','historyColour1':c,'historyColour2':c,'historyTime':3.0,'historySkipFrames':1},bounds)
definitions=[];segment_keys=set()
# 0.3.7 on the Live II: the menu opens (title + back arrow) but the list area
# stays dark. The SDK reference overlay differs from ours in two points:
# transparent background and a focusable overlay child. All menus now use a
# focusable child; the background differs per colour group so ONE device test
# tells which variant shows the entries (osc/env: transparent as documented,
# mod: dark solid, fx: light solid in case the list text is dark).
MENU_OVERLAY={'osc':'menu overlay','env':'menu overlay','mod':'menu overlay dark','fx':'menu overlay light'}
for color,accent in COLORS.items():
    for role in ['knob','bipolar','fader','toggle','menu','file']:
        # One focus stop per slot, as in the SDK's own generator (ui-reference
        # 5.2): the slot entry takes hardware focus and every child is 'No', so
        # a tap anywhere in the cell focuses it and the data wheel edits its
        # value (knob, fader, switch and choice cells alike).
        children=[g.entry('focus','Focus',g.focus_data(accent),SLOT,'No','Show','WhenFocussed'),
            ent('name','Label',g.label_data('Name',TEXT,SLOT_FONT),(0,0,SLOT_W,30 if FORCE else 26))]
        actions=g.control_actions('value overlay')
        if role in ['knob','bipolar']:
            children.extend([ent('knob','Knob',g.knob_filmstrip_data(f'large-{color}-{role}{DEF_SUFFIX}.png',127,'Vertical'),(8,30 if FORCE else 28,KNOB,KNOB)),
                ent('value','Label',g.label_data('Value',accent,SLOT_FONT),(16+KNOB,50,SLOT_W-24-KNOB,64))])
            if role=='bipolar':actions=g.control_actions('bipolar overlay')
        elif role=='fader':
            children.extend([ent('value','Label',g.label_data('Value',accent,SLOT_FONT),(0,24,SLOT_W,40)),
                ent('fader','Knob',g.knob_filmstrip_data(f'compact-{color}-fader{DEF_SUFFIX}.png',127,'Horizontal'),(6,78,FADER_W,FADER_H))])
        elif role=='toggle':
            children.extend([ent('switch','Button',g.button_data(f'large-{color}-on{DEF_SUFFIX}.png',f'large-{color}-off{DEF_SUFFIX}.png',1,1),(0,40,TOGGLE_W,TOGGLE_H)),
                ent('state','Label',g.label_data('Value',accent,SLOT_FONT),(TOGGLE_W+8,52,SLOT_W-TOGGLE_W-8,52))])
            actions=[g.action('Mouse Down','Q-Link','Data','')]
        elif role=='menu':
            # 0.3.9: exactly the slot the SDK's own generator uses for a choice with a
            # menu: the MPC's generic slider (draws name, value and a bar; drag or data
            # wheel steps through the choices) opening the list on double-tap/Enter.
            # 0.3.2-0.3.8 opened an EMPTY list from a label-only cell on Mouse Down.
            # 0.3.10, seen on the Live II through Remote Screen: the MenuOverlay list is
            # empty even in the unmodified SDK example (one blank row with a tick),
            # so it is an SDK/firmware limitation and is no longer used. A choice is
            # a Vitaleon-style fader that steps through its labels (drag, data wheel,
            # Q-Link) and opens the working KnobOverlay on double-tap, like any knob.
            children.extend([ent('value','Label',g.label_data('Value',accent,SLOT_FONT),(0,24,SLOT_W,40)),
                ent('fader','Knob',g.knob_filmstrip_data(f'compact-{color}-fader{DEF_SUFFIX}.png',127,'Horizontal'),(6,78,FADER_W,FADER_H))])
            actions=g.control_actions('value overlay')
        else:
            # 0.3.17: file selectors (wavetable, sample, LFO shape) were wheel-only.
            # They now carry the same fader as every other choice, so a table can be
            # chosen by touch; the data wheel and the double-tap overlay still work.
            children.extend([ent('field','Image',{'version':1,'colour':PANEL,'image':''},(0,22,SLOT_W,46)),
                ent('value','Label',g.label_data('Value',accent,SLOT_FONT-2),(6,24,SLOT_W-12,42)),
                ent('fader','Knob',g.knob_filmstrip_data(f'compact-{color}-fader{DEF_SUFFIX}.png',127,'Horizontal'),(6,78,FADER_W,FADER_H))])
        definitions.append(g.obj(('key',role+'-'+color),('value',g.component_definition(actions,children,'00000000'))))
definitions.append(g.obj(('key','value overlay'),('value',g.overlay_component('KnobOverlay',g.knob_overlay_data(),BG))))
definitions.append(g.obj(('key','bipolar overlay'),('value',g.overlay_component('KnobOverlay',g.knob_overlay_data(True),BG))))
def menu_overlay(key,background):  # kept for reference; unused since 0.3.10
    component=g.overlay_component('MenuOverlay',g.menu_overlay_data(),background)
    bounds=dict(dict(component)['componentsData'][0])['bounds'];bounds[:]=[(k,'Yes' if k=='acceptsHWFocus' else v) for k,v in bounds]
    definitions.append(g.obj(('key',key),('value',component)))
# menu_overlay('menu overlay','0')  # reference form: transparent, focusable list (the 0.3.8 background variants made no difference on the device)

# The play page keeps the same 112px dials while reserving the right third for
# the browser. Values sit below the dials, not squeezed beside them.
for color in COLORS:
    for role in ['knob','bipolar','fader','toggle']:
        children=[g.entry('focus','Focus',g.focus_data(COLORS[color]),'0 0 184 188','No','Show','WhenFocussed'),
            ent('name','Label',g.label_data('Name',TEXT,23,'horizontallyCentred verticallyCentred'),(0,0,184,30))]
        if role in ['knob','bipolar']:
            children.append(ent('knob','Knob',g.knob_filmstrip_data(f'large-{color}-{role}.png',127,'Vertical'),(36,32,112,112)))
        elif role=='fader':
            children.append(ent('fader','Knob',g.knob_filmstrip_data(f'home-fader-{color}.png',127,'Vertical'),(64,32,56,112)))
        else:children.append(ent('switch','Button',g.button_data(f'large-{color}-on.png',f'large-{color}-off.png',1,1),(24,56,136,72)))
        children.append(ent('value','Label',g.label_data('Value',COLORS[color],28,'horizontallyCentred verticallyCentred'),(0,146,184,40)))
        actions=g.control_actions('bipolar overlay' if role=='bipolar' else 'value overlay') if role!='toggle' else [g.action('Mouse Down','Q-Link','Data','')]
        definitions.append(g.obj(('key','home-'+role+'-'+color),('value',g.component_definition(actions,children,'00000000'))))

def parameter(name):return params[index(name)]

# Q-Link frame (0.3.22, user request): a thin outline around the four controls of the
# knob group in use, as other plugins show it. The MPC's own frame (qlinkBoundsData)
# is fixed per native tab and cannot follow these views, so the frame is drawn here and
# follows the plugin's 'UI Bank' (the group of the knob turned last).
frame_images=set()
def frame_image(w,h,color):
    name=f'qframe-{w}x{h}-{color}.png'
    if name not in frame_images:
        image=Image.new('RGBA',(w,h),(0,0,0,0));rgb=COLORS[color][2:]
        ImageDraw.Draw(image).rounded_rectangle((1,1,w-2,h-2),radius=10,outline=tuple(int(rgb[i:i+2],16) for i in (0,2,4))+(210,),width=2)
        image.save(skin/name);frame_images.add(name)
    return name
def qlink_frames(cells,section,color):
    frames=[]
    for group in range(0,len(cells),4):
        boxes=[tuple(map(int,dict(dict(c)['bounds'])['bounds'].split())) for c in cells[group:group+4]]
        row=len({(b[1],b[3]) for b in boxes})==1 and all(b2[0]-(b1[0]+b1[2])<=16 for b1,b2 in zip(boxes,boxes[1:]))
        if row:boxes=[(boxes[0][0],boxes[0][1],boxes[-1][0]+boxes[-1][2]-boxes[0][0],boxes[0][3])]
        for x,y,w,h in boxes:
            m=6 if row else 4
            frames.append(g.entry('Q-Link frame','Image',{'version':1,'colour':'0','image':frame_image(w+2*m,h+2*m,color)},f'{x-m} {y-m} {w+2*m} {h+2*m}','No','Hide','Always',
                invalidating=[f'IndexedEnabling/{section}/32/'+handle('UI Section'),f'IndexedEnabling/{group//4}/4/'+handle('UI Bank')]))
    return frames

# Q-Link slots (0.3.22). The 16 hardware knobs write the parameters 'UI Control 1..16';
# the plugin forwards a slot to the control of the visible section. A knob or fader
# on screen is bound to that same slot, so the MPC moves the control it wrote, as
# with any plugin whose knobs map straight to its controls. 0.3.20 and earlier bound
# it to the sound parameter and it stood still while the knob turned; 0.3.21 asked
# for display updates instead, which made every control stutter. 'Target' keeps the
# sound parameter in the entry for the checks; the MPC ignores it. Switches, lists
# and file selectors stay on their own parameter: their steps and labels belong to it.
GROUP_SLOT=None
def group_page_handle(group):return handle(f'Group {group} Page')
def group_members(group):return [i for i,v in enumerate(VIEWS) if v['group']==group and not v.get('hidden')]
def group_pages(group):return [i for i,v in enumerate(VIEWS) if v['group']==group]
MOD_STEPS=13
def mod_page_index(page):
    # The MOD tab's one strip: LFO 1..8, MATRIX..CURVE, then the pickers. The
    # index counts the group's visible members, the same way the plugin does.
    v=VIEWS[page]
    if v['group'] not in (4,6):return None
    if v.get('hidden'):return 12
    visible=[j for j,x in enumerate(VIEWS) if x['group']==v['group'] and not x.get('hidden')]
    return (0 if v['group']==4 else 8)+visible.index(page)
def page_nav(page):
    # Inside a tab bar the page parameter counts the pages of one group.
    if FORCE:
        k=mod_page_index(page)
        if k is not None and k<12:return 'MOD Page',k,MOD_STEPS
        own=group_pages(VIEWS[page]['group']);return f"Group {VIEWS[page]['group']} Page",own.index(page),len(own)
    return 'UI Page',page,len(VIEWS)
def slot_data(title,name,slot):return [('Data','WithName['+title+']/'+handle((f'Group {GROUP_SLOT} Slot' if GROUP_SLOT is not None else 'UI Control')+f' {slot+1}')),('Target',handle(name))]
def control(name,title,x,y,color='osc',visible=None,compact=False,slot=None):
    p=parameter(name);choices=p.get('choices',[])
    if p['kind']=='bool':role='toggle'
    elif len(choices)>1:role='menu'
    elif 'File' in name or 'Table' in name or name=='Preset':role='file'
    elif name.lower().replace('_',' ').split()[-1] in ['level','volume','master','mix','wet','attack','decay','sustain','release','hold','delay'] or name.endswith(('_decay_time','_delay_time',' delay_time')):role='fader'
    elif abs(p['default']-.5)<.0001 and p['display'].startswith('0'):role='bipolar'
    else:role='knob'
    if 1<len(choices)<=4 and p['kind']!='bool':
        # Short enumerations are immediately tappable, not a rotary mode selector.
        role='segments';key='segments-'+str(p['index'])+'-'+color
        children=[g.entry('focus','Focus',g.focus_data(COLORS[color]),SLOT,'No','Show','WhenFocussed'),ent('name','Label',g.label_data('Name',TEXT,SLOT_FONT),(0,0,SLOT_W,30))]
        for n,choice in enumerate(choices):
            px=6+(n%2)*SEG_DX;py=32+(n//2)*SEG_DY
            children.append(ent('choice','Button',g.button_data(f'segment-choice-{color}-on{DEF_SUFFIX}.png',f'segment-choice-{color}-off{DEF_SUFFIX}.png',n,len(choices)),(px,py,SEG_W,SEG_H)))
            words=choice.split();lines=['']
            for word in words:
                if len(lines[-1])+len(word)>12 and lines[-1]:lines.append(word)
                else:lines[-1]=(lines[-1]+' '+word).strip()
            for line,text in enumerate(lines[:2]):children.append(ent('choice text','Label',g.label_data('Text',TEXT,22 if FORCE else 18,'horizontallyCentred verticallyCentred',text),(px+4,py+(4+line*20 if len(lines)>1 else 10),SEG_W-8,24)))
        if key not in segment_keys:
            definitions.append(g.obj(('key',key),('value',g.component_definition([g.action('Mouse Down','Q-Link','Data','')],children,'00000000'))));segment_keys.add(key)
    else:key=role+'-'+color
    if compact:
        assert role in ['knob','bipolar','fader','toggle'],(name,role)
        if name.startswith('Env ') or name in ['Voices','Home Attack','Home Release']:color='env'
        elif name.startswith('filter_') or name in ['Home Cutoff','Home Resonance']:color='fx'
        elif name.startswith('macro_'):color='mod'
        key='home-'+role+'-'+color
    data=slot_data(title,name,slot) if slot is not None and role in ['knob','bipolar','fader'] else [('Data','WithName['+title+']/'+handle(name))]
    return g.entry(title,key,{'version':1,'handleName':'Data'},f'{x} {y} '+('184 188' if compact else f'{SLOT_W} {SLOT_H}'),'Yes','Hide' if visible else 'Show','Always',data+([('Menu',handle(name))] if role=='menu' else []),invalidating=visible)

def home_browser(children):
    # The Force is wider: the browser panel moves left and gains the space.
    divid=700 if FORCE else 800;rx=divid+28;rw=SCREEN_RIGHT-rx-20
    half=(divid-48)//2
    children.append(rect('browser divider',(divid,96,1,516),LINE))
    children.append(label('Loaded sound',(24,98,divid-40,24),MUTED,20))
    for part in range(2):children.append(label('Loaded name',(24+part*(half+8),126,half,32),COLORS['env'],24,value='library_loaded_'+str(part)))
    for part in range(4):children.append(label('Import status',(24+(part%2)*(half//2+8),166+(part//2)*25,half//2,24),COLORS['mod'],17,value='display_load_text_'+str(part)))
    data={'version':2,'onImage':'home-filter-on.png','offImage':'home-filter-off.png','buttonId':1,'numButtonsInGroup':1,'handleName':handle('Home Filter'),'gestureBehaviour':'Instant'}
    children.append(ent('Filter 1 enable','Button',data,(584,160,200,52),True))
    children.append(label('FILTER 1',(596,160,116,52),TEXT,19))
    children.append(label('Filter state',(712,160,66,52),TEXT,21,value='Home Filter'))
    children.append(label('MPC PRESET',(rx,104,rw,40),TEXT,30))
    children.append(label('All sounds are in the top bar.',(rx,154,rw,32),MUTED,23))
    children.append(label('Choose a name to load it.',(rx,190,rw,32),MUTED,23))
    children.append(rect('sound count divider',(rx,248,rw,1),LINE))
    children.append(label('Available sounds',(rx,274,rw,30),MUTED,22))
    children.append(label('Inventory',(rx,316,rw,36),COLORS['env'],24,value='library_page_text'))
    children.append(label('Bank',(rx,388,rw,30),MUTED,22))
    for part in range(2):children.append(label('Bank name',(rx,430+part*30,rw,30),TEXT,23,value='library_bank_text_'+str(part)))
    children.append(label('Save edits with the MPC save icon.',(rx,548,rw,30),MUTED,20))

def envelope(children):
    data={'version':1,'envelopeType':'DAHDSR','envelopeStyle':'WithoutHeader','envelopeName':'ENVELOPE','backgroundColour':'ff1c1f29','scaleColour':LINE,'lineColour':COLORS['env'],'fillColour':'2064d9ba','highlightColour':TEXT,'lineWidth':3,'handleWidth':40,'handleHeight':40,'handleShape':'Circle','handleLineWidth':2,'handleLineColour':COLORS['env'],'handleFillColour':BG,'handleTextColour':TEXT,'handleLineWidthOnPressed':2,'handleLineColourOnPressed':TEXT,'handleFillColourOnPressed':COLORS['env'],'handleTextColourOnPressed':BG,'handleArrowLineColourOnPressed':TEXT,'handleArrowFillColourOnPressed':COLORS['env']}
    for stage in ['delay','attack','hold','decay','sustain','release']:data[stage+'Handle']=handle('Env '+stage)
    for stage in ['attack','decay','release']:data[stage+'ShapeHandle']=handle('Env '+stage+'_power')
    data.update(forceHideShapeHandles=False,decayAndReleaseCurvesLinked=False,curveType='Exponential')
    children.append(ent('editable envelope','Envelope',data,(24,100,full(728),164),True))
    children.append(trace('Envelope output',(full(768),100,full(232),164),'display_env_value','env'))
def pretty(k):
    words=k.replace('_',' ').replace('spectral morph','spectral').replace('distortion','warp').replace('unison voices','unison').replace('keytrack transpose','key transpose').replace('keytrack tune','key tune')
    return words.title()
from surface_spec import VIEWS,GROUPS,TARGET_GROUPS,write_header
write_header()
definitions.append(g.obj(('key','effect-toggle'),('value',g.component_definition([g.action('Mouse Down','Q-Link','Data','')],[
    ent('Effect name','Label',g.label_data('Name',TEXT,SLOT_FONT),(0,0,SLOT_W,24 if FORCE else 26)),
    ent('Effect switch','Button',g.button_data(f'large-fx-on{DEF_SUFFIX}.png',f'large-fx-off{DEF_SUFFIX}.png',1,1),(0,24 if FORCE else 40,TOGGLE_W,TOGGLE_H)),
    ent('Effect state','Label',g.label_data('Value',COLORS['fx'],SLOT_FONT),(TOGGLE_W+8,28,SLOT_W-TOGGLE_W-8,52)),
],'00000000'))))
pages=[]
def nav_button(text,x,y,w,h,param,value=1,count=1,color='osc'):
    # Native-sized caption assets; no text widget ever covers a button.
    key=hashlib.sha256(f'{text}:{w}:{h}:{color}'.encode()).hexdigest()[:16]
    for state in ['off','on']:
        caption=text[0 if state=='off' else 1] if isinstance(text,tuple) else text
        filename=f'nav-{key}-{state}.png'
        im=Image.new('RGBA',(w,h),'#373045' if state=='on' else '#222531');d=ImageDraw.Draw(im)
        d.rectangle((0,0,w-1,h-1),outline='#'+(COLORS[color] if state=='on' else LINE)[2:],width=2 if state=='on' else 1)
        size=22;font=ImageFont.truetype(str(root/'fonts/Roboto-Regular.ttf'),size)
        while d.textlength(caption,font=font)>w-12 and size>13:size-=1;font=ImageFont.truetype(str(root/'fonts/Roboto-Regular.ttf'),size)
        assert d.textlength(caption,font=font)<=w-10,(caption,w)
        bb=d.textbbox((0,0),caption,font=font);d.text(((w-d.textlength(caption,font=font))/2,(h-(bb[3]-bb[1]))/2-bb[1]),caption,font=font,fill='#edf0f7');im.save(skin/filename)
    data={'version':2,'onImage':f'nav-{key}-on.png','offImage':f'nav-{key}-off.png','buttonId':value,'numButtonsInGroup':count,'handleName':handle(param),'gestureBehaviour':'Instant'}
    return ent(text[0] if isinstance(text,tuple) else text,'Button',data,(x,y,w,h),True)

def add_library(c):
    # 0.3.19 layout: place line, three commands, six rows of KIND | NAME | button.
    # A row offers OPEN for a drive, folder or bank and LOAD for a sound (state 1 / 2
    # of storage_available_N). REFRESH is gone: DRIVES rescans.
    c.append(label('Directory',(24,76,700,26),MUTED,18,value='storage_path_0'))
    for n,(caption,param,w) in enumerate([('DRIVES','Drives',150),('UP','Up Folder',150),('INSTALLED','Installed Banks',190)]):c.append(nav_button(caption,[24,182,340][n],108,w,52,param))
    c.append(label('Page',(560,112,190,36),MUTED,22,value='storage_page'))
    c.append(nav_button('PREV',760,108,116,52,'Previous Files'));c.append(nav_button('NEXT',884,108,116,52,'Next Files'))
    row_w=full(976);action_x=full(840)
    for row in range(6):
        y=174+row*54;available=handle(f'storage_available_{row}')
        c.append(rect('file separator',(24,y+51,row_w,1),LINE))
        c.append(label('File kind',(24,y+12,96,30),MUTED,18,value=f'storage_row_{row*2}'))
        c.append(label('File name',(124,y+4,action_x-136,44),TEXT,24,value=f'storage_row_{row*2+1}'))
        c.append(conditional(nav_button('OPEN',action_x,y,156,52,f'Open Row {row+1}'),['IndexedEnabling/1/3/'+available]))
        c.append(conditional(nav_button('LOAD',action_x,y,156,52,f'Open Row {row+1}',color='env'),['IndexedEnabling/2/3/'+available]))
    for part in range(4):c.append(label('Browser status',(24+part*full(244),512,full(236),26),COLORS['mod'],18,value=f'storage_status_{part}'))  # one reading line, like HOME

# ---- Serum-style modulation matrix and its two-step target picker ------------
MATRIX_VIEW=next(i for i,v in enumerate(VIEWS) if v['chart']=='mxtable')
GROUP_VIEW=next(i for i,v in enumerate(VIEWS) if v['chart']=='pickgroup')
TARGET_VIEW=next(i for i,v in enumerate(VIEWS) if v['chart']=='picktarget')
def strip(name,size,frames,draw):
    if (skin/name).exists():return
    w,h=size;image=Image.new('RGBA',(w,h*frames))
    for k in range(frames):
        frame=Image.new('RGBA',(w,h));draw(ImageDraw.Draw(frame),k/(frames-1));image.paste(frame,(0,k*h))
    image.save(skin/name)
MOD=tuple(bytes.fromhex(COLORS['mod'][2:]))
def draw_fader(d,v):
    d.rounded_rectangle((4,22,116,30),radius=4,fill='#454c61');d.rounded_rectangle((4,22,4+round(v*112),30),radius=4,fill=MOD)
    x=4+round(v*112);d.rounded_rectangle((x-5,8,x+5,44),radius=3,fill='#edf0f7')
def draw_bipolar(d,v):
    d.rounded_rectangle((4,22,116,30),radius=4,fill='#454c61');x=4+round(v*112);mid=60
    d.rectangle((min(x,mid),22,max(x,mid),30),fill=MOD);d.rectangle((mid,16,mid,36),fill='#a0a7b7');d.rounded_rectangle((x-5,8,x+5,44),radius=3,fill='#edf0f7')
strip('mx-fader.png',(120,52),127,draw_fader);strip('mx-bipolar-120.png',(120,52),127,draw_bipolar)
# Matrix cells: a value widget on every cell, so a tap and the data wheel always
# reach the row (a label-only cell swallowed every touch, upstream PR #5).
definitions.append(g.obj(('key','mx-source'),('value',g.component_definition(g.control_actions('value overlay'),[
    g.entry('focus','Focus',g.focus_data(COLORS['mod']),'0 0 228 52','No','Show','WhenFocussed'),
    ent('value','Label',g.label_data('Value',TEXT,18),(4,2,92,48)),
    ent('fader','Knob',g.knob_filmstrip_data('mx-fader.png',127,'Horizontal'),(100,0,120,52))],'00000000'))))
definitions.append(g.obj(('key','mx-amount'),('value',g.component_definition(g.control_actions('bipolar overlay'),[
    g.entry('focus','Focus',g.focus_data(COLORS['mod']),'0 0 208 52','No','Show','WhenFocussed'),
    ent('fader','Knob',g.knob_filmstrip_data('mx-bipolar-120.png',127,'Horizontal'),(0,0,120,52)),
    ent('value','Label',g.label_data('Value',COLORS['mod'],18),(122,2,82,48))],'00000000'))))
definitions.append(g.obj(('key','mx-target'),('value',g.component_definition(g.control_actions('value overlay'),[
    g.entry('focus','Focus',g.focus_data(COLORS['mod']),'0 0 222 52','No','Show','WhenFocussed'),
    ent('value','Label',g.label_data('Value',TEXT,18),(4,2,92,44)),
    ent('fader','Knob',g.knob_filmstrip_data('mx-fader.png',127,'Horizontal'),(96,0,120,52))],'00000000'))))
def matrix_table(children):
    # The Force has no sidebar, so the table spreads over the full width. Its Q-Link
    # frame surrounds the whole four-row block of the bank; around the amount column
    # only, it read like a stray box under the AMOUNT caption.
    src,amt,tgt=(full(60),full(292),full(504)) if FORCE else (60,292,504)
    pick,pitch=(full(730),full(68)) if FORCE else (730,68)
    for x,text in [(src,'SOURCE'),(amt,'AMOUNT'),(tgt,'TARGET')]:children.append(label(text,(x,128,200,20),MUTED,15))
    for page in range(8):
        show=[f'IndexedEnabling/{page}/32/'+handle('UI Section')]
        for row in range(8):
            route=page*8+row+1;y=148+row*52;knob=handle(f'UI Control {row+1}')
            cell=lambda key,param,x,w:g.entry(f'route {route}',key,{'version':1,'handleName':'Data'},f'{x} {y} {w} 52','Yes','Hide','Always',[('Data','WithName[Route '+str(route)+']/'+handle(param)),('QTarget',knob)],invalidating=show)
            if row%4==0 and not FORCE:
                children.append(g.entry('Q-Link frame','Image',{'version':1,'colour':'0','image':frame_image(216,216,'mod')},f'288 {y-4} 216 216','No','Hide','Always',invalidating=show+[f'IndexedEnabling/{row//4}/4/'+handle('UI Bank')]))
            children.append(conditional(label(f'{route:02d}',(24,y+4,32,44),MUTED,16),show))
            children.append(cell('mx-source',f'Route {route} Source',src,228))
            amount=g.entry(f'route {route}','mx-amount',{'version':1,'handleName':'Data'},f'{amt} {y} 208 52','Yes','Hide','Always',[('Data','WithName[Route '+str(route)+']/'+knob),('Target',handle(f'modulation_{route}_amount')),('QTarget',knob)],invalidating=show)
            children.append(amount)
            children.append(cell('mx-target',f'Route {route} Target',tgt,222))
            children.append(conditional(nav_button('PICK',pick,y,64,52,'UI Pick',route,65,'mod'),show))
            children.append(conditional(nav_button('BIP',pick+pitch,y,64,52,f'modulation_{route}_bipolar',color='mod'),show))
            children.append(conditional(nav_button('STER',pick+2*pitch,y,64,52,f'modulation_{route}_stereo',color='mod'),show))
            children.append(conditional(nav_button('MUTE',pick+3*pitch,y,64,52,f'modulation_{route}_bypass',color='fx'),show))
def pick_groups(children):
    children.append(label('for route',(300,72,110,38),MUTED,24));children.append(label('Route',(410,72,100,38),COLORS['mod'],30,value='Route Select'))
    children.append(nav_button('BACK',886,76,110,52,*page_nav(MATRIX_VIEW),'mod'))
    children.append(nav_button('NO TARGET',766,76,110,52,'Target',0,len(DESTINATIONS)+1,'fx'))
    # 26 target groups: six 156 px columns keep five rows inside the content area.
    for k,(name,_) in enumerate(TARGET_GROUPS):
        row,col=divmod(k,6);children.append(nav_button(name,24+col*164,150+row*68,156,60,'UI Section',k+1,32,'mod'))
def pick_targets(children):
    children.append(nav_button('BACK',886,76,110,52,*page_nav(MATRIX_VIEW),'mod'))
    children.append(nav_button('GROUPS',766,76,110,52,*page_nav(GROUP_VIEW),'mod'))
    for k,(name,items) in enumerate(TARGET_GROUPS):
        show=[f'IndexedEnabling/{k+1}/32/'+handle('UI Section')]
        children.append(conditional(label(name,(300,72,456,36),COLORS['mod'],26),show))
        for n,destination in enumerate(items):
            # The picker lists destination names, it is not the Q-Link control
            # grid: four 232 px columns keep a 32-entry group on one screen.
            row,col=divmod(n,4);caption=target_name(destination)
            children.append(conditional(nav_button(caption,24+248*col,140+row*52,232,52,'Target',DESTINATIONS.index(destination)+1,len(DESTINATIONS)+1,'mod'),show))
# Three wide columns (308 px at x = 24, 356, 688): two rows of three fill the
# content area, so a section carries at most six controls. The oscillator's
# sound section shows its 16:9 graph in the first cell row and four controls.
def cell_position(chart,section,slot):
    if chart=='effects':
        row,col=divmod(slot,3);return col_x(col),152+row*132
    if chart in ['env','lfo','curve']:
        assert slot<3,(chart,slot)
        return col_x(slot),364
    if isinstance(chart,int) and section==0:
        if slot==0:return col_x(2),166
        return col_x(slot-1),372
    row,col=divmod(slot,3);return col_x(col),166+190*row
from surface_spec import DESTINATIONS
all_children=[]
version=json.loads((project/'plugin.json').read_text())['version']
member_rows={};page_entries={}
# Six tabs: LFO and OTHER share MOD, so no group ever needs its own pager.
FORCE_TABS=[('HOME',[0]),('OSC',[1]),('FILTER',[2]),('ENV',[3]),('MOD',[4,6]),('FX',[5])] if FORCE else []
SHORT_CAPTIONS={'EXPRESSION':'EXPR','MOD CURVE':'CURVE'}
if FORCE:
    for tab_name,groups in FORCE_TABS:
        members=[(i,p) for grp in groups for i,p in enumerate(VIEWS) if p['group']==grp and not p.get('hidden')]
        width=(SCREEN_RIGHT-24-7*(len(members)-1))//len(members)
        row=[]
        for slot,(i,p) in enumerate(members):
            group=p['group'];all_pages=group_pages(group);page_slot=all_pages.index(i)
            caption=SHORT_CAPTIONS.get(p['tab'].upper(),p['tab'])
            x=24+slot*(width+7)
            k=mod_page_index(i)
            if k is not None and k<12:
                row.append(nav_button(caption,x,4,width,56,'MOD Page',k,MOD_STEPS,p['color']))
            else:
                row.append(nav_button(caption,x,4,width,56,f'Group {group} Page',page_slot,len(all_pages),p['color']))
        member_rows[tab_name]=row
else:
    for group,group_name in enumerate(GROUPS):
        members=[(i,p) for i,p in enumerate(VIEWS) if p['group']==group and not p.get('hidden')]
        width=(SCREEN_RIGHT-24-8*(len(members)-1))//len(members)
        row=[conditional(nav_button(p['tab'],24+slot*(width+8),4,width,56,'UI Page',i,len(VIEWS),p['color']),[f'IndexedEnabling/{group}/7/'+handle('UI Group')]) for slot,(i,p) in enumerate(members)]
        member_rows[group]=row
        all_children.extend(row)
for i,view in enumerate(VIEWS):
    GROUP_SLOT=view['group'] if FORCE else None
    title=view['title'];chart=view['chart'];color=view['color'];sections=view['sections'];children=[]
    pages.append(dict(family=view['group'],sub=view['member'],title=title,items=[x for sec in sections for x in sec['items']],chart=chart,color=color,subtitle='',sections=sections))
    # ENV/LFO instances share their actual proxy-backed graph/control view.
    # Do not instantiate eight copies of 384 graph segments on a small MPC.
    # The MPC reuses one proxy-backed page per module; the Force tab bar owns
    # its pages, so every ENV/LFO button needs a page of its own there.
    if not FORCE and view['group'] in [3,4] and view['member']>1:continue
    # Section buttons are right-aligned inside the content area, before the title
    # is placed, so the two never overlap (the caption is baked into the button).
    section_buttons=[]
    if chart!='library' and len(sections)>1 and chart!='picktarget':
        end=696 if chart=='home' else SCREEN_RIGHT-4
        wide=len(sections)<=4;button_w=124 if wide else 88;step=132 if wide else 96
        for section,part in enumerate(sections):
            section_buttons.append((part['title'],end-len(sections)*step+section*step,button_w))
    title_room=min(440,section_buttons[0][1]-32) if section_buttons else (330 if chart=='mxtable' else 440)
    if chart!='library':
        # Seven LFO sections need the top line for their buttons, so the module
        # selector moves next to the version label on ENV/LFO pages.
        if view['group'] in [3,4]:
            children.append(label('ENVELOPE' if view['group']==3 else 'LFO',(24,72,296,38),TEXT,30))
        else:children.append(label(title.upper() if chart not in ['pickgroup','picktarget'] else 'CHOOSE TARGET',(24,72,title_room,38),TEXT,30))
        children.append(label('VITALEON '+version,(24,110,150 if view['group'] in [3,4] else (min(260,section_buttons[0][1]-32) if section_buttons else 260),24),MUTED,17))
        if view['group'] in [3,4]:
            children.append(label('Selected module',(190,110,130,24),MUTED,20,value='Env Select' if view['group']==3 else 'LFO Select'))
        if chart=='home':
            children.append(nav_button(('FILTER 1 OFF','FILTER 1 ON'),700,76,296,52,'Home Filter',color='fx'))
            for part in range(4):children.append(label('Import status',(24+part*244,134,236,26),COLORS['mod'],18,value=f'display_load_text_{part}'))
        for section,(title_text,button_x,button_w) in enumerate(section_buttons):
            children.append(nav_button(title_text,button_x,76,button_w,52,'UI Section',section,32,color))
        # Large graphs (user request): oscillator 560x315 (16:9), envelope and
        # LFO/curve 240 px high with a single row of three controls below.
        # 0.3.7 compromise after "too small" (0.3.5) and "way too big" (0.3.6).
        if isinstance(chart,int):
            # Only the sound section shows the graph; the other sections use its
            # two cell rows for six more controls. The graph sits in its own
            # transparent container so one IndexedEnabling hides it as a whole
            # (the bare grid rectangles carry no handle of their own).
            graph=[];wave_bars(graph,chart,24,140,384,216)
            for e in graph:
                b=dict(e)['bounds'];x,y,w,h=map(int,dict(b)['bounds'].split())
                b[:]=[(k,f'{x} {y-64} {w} {h}' if k=='bounds' else v) for k,v in b]
            graph_key=f'osc-graph-{chart}'
            definitions.append(g.obj(('key',graph_key),('value',g.component_definition([],graph,'00000000'))))
            # Drawn before the section buttons below, so the buttons stay on top
            # and keep their taps. A wrapper that holds focusable children and
            # is shown/hidden uses "Children" (ui-reference 7.3), never "No".
            children.insert(0,g.entry('Oscillator graph',graph_key,{'version':1,'handleName':'Data'},'0 64 1280 500','Children','Hide','Always',[('Data',handle('UI Section'))],invalidating=[f'IndexedEnabling/0/32/'+handle('UI Section')]))
        elif chart in ['lfo','curve']:
            curve_w=full(960)//GRAPH['kCurveColumns']*GRAPH['kCurveColumns']
            curve_bars(children,24,140,curve_w,190)
        elif chart=='mxtable':matrix_table(children)
        elif chart=='pickgroup':pick_groups(children)
        elif chart=='picktarget':pick_targets(children)
        elif chart=='env':
            envelope(children)
            # Preserve the editable stage controls, with space for the footer.
            for e in children:
                d=dict(dict(e)['componentData']);b=dict(e)['bounds']
                if d['type']=='Envelope':b[:]=[(k,f'24 140 {full(728)} 190' if k=='bounds' else v) for k,v in b]
                if d['type']=='Plotter':b[:]=[(k,f'{full(768)} 140 {full(232)} 190' if k=='bounds' else v) for k,v in b]
        for section,part in enumerate(sections):
            if chart in ['mxtable','pickgroup','picktarget']:break
            cells=[]
            for slot,name in enumerate(part['items']):
                x,y=cell_position(chart,section,slot)
                caption=pretty(name)
                if view['group']==5:
                    for prefix in ['Compressor ','Chorus ','Delay ','Warp ','Eq ','Filter Fx ','Flanger ','Phaser ','Reverb ']:
                        if caption.startswith(prefix):caption=caption[len(prefix):]
                # Only the page's own module prefix may go: 'Osc 1 Random Phase' is
                # the oscillator's phase randomisation, not a control of the Random
                # module, so the first match ends the stripping (it used to end as
                # 'Phase', the same caption as the plain phase control).
                for prefix in ['Osc '+str(view['member'])+' ','Filter 1 ','Filter 2 ','Env ','Lfo ','Random ']:
                    if caption.startswith(prefix):caption=caption[len(prefix):];break
                caption=caption.replace('Spectral Amount','Spectral Amt').replace('Warp Amount','Warp Amount').replace('Unison Detune','Detune').replace('Smooth Interpolation','Smooth Frames').replace('Keytrack','Key Track').replace('Transpose Quantize','Pitch Quantize').replace('Lower Threshold','Lower Thresh.').replace('Upper Threshold','Upper Thresh.').replace('Dry Wet','Mix').replace('Home ','').replace('Macro Control','Macro')
                if chart=='effects' and section==0:
                    row,col=divmod(slot,3);caption=name[:-3].replace('_',' ').title()
                    cell=g.entry(caption,'effect-toggle',{'version':1,'handleName':'Data'},f'{col_x(col)} {152+row*132} {SLOT_W} 112','Yes','Hide','Always',[('Data','WithName['+caption+']/'+handle(name))],invalidating=[f'IndexedEnabling/{section}/32/'+handle('UI Section')])
                else:
                    control_color=color
                    if chart=='home':
                        if name in ['Voices','Home Attack','Home Release']:control_color='env'
                        elif name.startswith('filter_') or name in ['Home Cutoff','Home Resonance','Home Filter']:control_color='fx'
                        elif name.startswith('macro_'):control_color='mod'
                    cell=control(name,caption,x,y,control_color,visible=[f'IndexedEnabling/{section}/32/'+handle('UI Section')],slot=slot)
                dict(dict(cell)['handle remapping'])['map'].append(g.obj(('key','QTarget'),('value',handle(f'Group {GROUP_SLOT} Slot {slot+1}' if GROUP_SLOT is not None else f'UI Control {slot+1}'))))
                cells.append(cell)
            # Frames first: they lie under the controls and take no touch. The Force
            # has eight Q-Links in two banks, so its knobs address the eight visible
            # slots directly; a four-slot bank outline would only mislead there.
            if not FORCE:children.extend(qlink_frames(cells,section,color))
            children.extend(cells)
        if chart=='home':
            # Below the second control row (ends at y = 512), like every page.
            children.append(label('Active bank',(24,520,140,24),MUTED,18))
            for part in range(2):children.append(label('Bank',(168+part*330,520,320,24),COLORS['env'],18,value=f'active_bank_{part}'))
            children.append(nav_button('LIBRARY',SCREEN_RIGHT-24-156,512,156,52,*page_nav(3)))
    else:add_library(children)
    if title=='Advanced' and __import__('os').environ.get('VITALEON_DIAG'):  # device diagnostic only
        children.append(label('DIAGNOSTIC: a test chord plays on every page except HOME',(24,352,1000,30),COLORS['mod'],20))
        children.append(label('blk mean/max us   gap max us between callbacks   late callbacks   wait max us for fx core   n host calls/s   call mean/max us   t seconds',(24,386,1230,26),MUTED,17))
        for part in range(4):children.append(label('Diag status',(24+part*308,424,304,40),TEXT,22,value=f'display_load_text_{part}'))
    for e in children:
        b=dict(e)['bounds'];x,y,w,h=map(int,dict(b)['bounds'].split())
        b[:]=[(k,f'{x} {y-64} {w} {h}' if k=='bounds' else v) for k,v in b]
    key=f'view-{i}'
    definitions.append(g.obj(('key',key),('value',g.component_definition([],children,BG))))
    if FORCE:
        # Hidden pages (LIBRARY, unused FX pages) still get a real slot; the
        # strip just does not show them, so they must not default to slot 0.
        own=group_pages(view['group']);slot=own.index(i)
        k=mod_page_index(i)
        if k is not None and k<12:
            # LFO and OTHER share the MOD tab through one parameter, so exactly
            # one of its pages is on screen and exactly one button is lit.
            conditions=[f'IndexedEnabling/{k}/{MOD_STEPS}/'+handle('MOD Page')]
        else:
            conditions=[f'IndexedEnabling/{slot}/{len(own)}/'+group_page_handle(view['group'])]
    else:conditions=[f'IndexedEnabling/{view["group"]}/7/'+handle('UI Group') if view['group'] in [3,4] else f'IndexedEnabling/{i}/{len(VIEWS)}/'+handle('UI Page')]
    # A hidden-on-invalid container needs a valid model of its own. Child
    # bindings alone do not make it valid (0.3.0 showed only navigation on MPC).
    # "Children": the page container itself takes no focus, but everything in
    # it may. With "No" nothing on any page could be focused or tapped (7.3).
    entry=g.entry(title,key,{'version':1,'handleName':'Data'},'0 64 1280 500','Children','Hide','Always',[('Data',group_page_handle(view['group']) if FORCE else handle('UI Page'))],invalidating=conditions)
    if FORCE:page_entries.setdefault(view['group'],[]).append(entry)
    else:all_children.append(entry)
FOOT_W=(SCREEN_RIGHT-72)//7;FOOT_STEP=FOOT_W+8
foot=[] if FORCE else [nav_button(name,24+FOOT_STEP*group,568,FOOT_W,56,'UI Group',group,7,['env','osc','fx','env','mod','fx','mod'][group]) for group,name in enumerate(GROUPS)]

# Touching a visible control selects the bank containing its live Q-Link proxy.
for definition in definitions:
    value=dict(dict(definition)['value'])
    for action in value['actions']:
        a=dict(action)
        if a.get('handler')=='Q-Link':action[:]=[(k,'QTarget' if k=='handleName' else v) for k,v in action]
if FORCE:
    tabs=[]
    for t,(tab_name,groups) in enumerate(FORCE_TABS):
        children=list(member_rows[tab_name])
        for grp in groups:children+=page_entries.get(grp,[])
        tabs.append({'version':1,'tabName':tab_name,'fnKeyIndex':t,'fnKeySubIndex':0,'componentDefinition':g.component_definition([],children,BG),'qlinkBoundsData':[]})
else:
    all_children.extend(e for group in range(len(GROUPS)) for e in page_entries.get(group,[]))
    all_children.extend(foot)
    tabs=[{'version':1,'tabName':'VITALEON','fnKeyIndex':0,'fnKeySubIndex':0,'componentDefinition':g.component_definition([],all_children,BG),'qlinkBoundsData':[]}]
q={}
for slot in range(16):row,col=divmod(slot,4);q[f'Q-Link {13+col-row*4}']=index(f'UI Control {slot+1}')
if FORCE:
    maps=[]
    for t,(tab_name,groups) in enumerate(FORCE_TABS):
        # A tab owns one bank; the MOD tab uses the LFO bank for both groups.
        bank=groups[0]
        gm={f'Q-Link {slot+1}':index(f'Group {bank} Slot {slot+1}') for slot in range(8)}
        maps.append({'Tab':t+1,'SubTab':1,'Bank Direction':'RowTopDown','Q-Links':gm})
else:maps=[{'Tab':1,'SubTab':1,'Bank Direction':'RowTopDown','Q-Links':q}]
replacements=[{'key':'Parameter '+str(p['index']),'value':p['choices']} for p in params if p['name']!='Full Engine' and len(p.get('choices',[]))>1]
tui={'pageData':{'version':1,'componentDefinitions':{'version':2,'importFiles':[],'localComponentDefinitions':definitions},'replacementStrings':replacements,'info':{'version':1,'type':'CompleteDescription'},'tabs':tabs}}
qlinks={'version':4,'info':{'version':1,'type':'CompleteDescription'},'Screen Mode Q-Links':{'version':4,'map':maps},'Program Mode Q-Links':{f'Q-Link {i+1}':index(n) for i,n in enumerate(['macro_control_1','macro_control_2','macro_control_3','macro_control_4','filter_1_cutoff','filter_1_resonance','filter_2_cutoff','filter_2_resonance','Env attack','Env decay','Env sustain','Env release','volume','Voices','osc_1_wave_frame','osc_1_unison_voices'])}}
for state in ['off','on']:
    img=Image.new('RGBA',(176,56),'#373045' if state=='on' else '#222531');d=ImageDraw.Draw(img)
    d.rectangle((0,0,175,55),outline='#bca3ff' if state=='on' else '#454c61',width=2 if state=='on' else 1)
    img.save(skin/f'panel-{state}.png')
# Original native-size touch backgrounds; labels remain dynamic native text.
for kind,size in [('bank',(116,52)),('row',(440,52)),('select',(96,52)),('page',(104,60)),('load',(216,60)),('mode',(216,56)),('filter',(200,52))]:
    for state in ['off','on']:
        selected=kind=='load' or (kind in ['row','select','mode','filter'] and state=='on')
        img=Image.new('RGBA',size,'#373045' if selected else '#222531');d=ImageDraw.Draw(img)
        d.rectangle((0,0,size[0]-1,size[1]-1),outline='#bca3ff' if selected else '#454c61',width=1)
        img.save(skin/f'home-{kind}-{state}.png')
for color,accent in COLORS.items():
    strip=Image.new('RGBA',(56,112*127))
    for i in range(127):
        image=Image.new('RGBA',(112,224));d=ImageDraw.Draw(image);y=200-round(i*176/126)
        d.rounded_rectangle((46,16,66,208),radius=10,fill='#454c61')
        d.rounded_rectangle((46,min(y,188),66,208),radius=10,fill='#'+accent[2:])
        d.rounded_rectangle((16,y-12,96,y+12),radius=5,fill='#edf0f7')
        strip.paste(image.resize((56,112),Image.Resampling.LANCZOS),(0,i*112))
    strip.save(skin/f'home-fader-{color}.png')
for kind,size in [('arrow',(64,60)),('page',(160,60)),('row',(744,60)),('select',(112,60)),('load',(424,64))]:
    for state in ['off','on']:
        fill='#373045' if kind=='load' or (kind in ['row','select'] and state=='on') else '#222531'
        border='#bca3ff' if kind=='load' or (kind in ['row','select'] and state=='on') else '#353949'
        img=Image.new('RGBA',size,fill);d=ImageDraw.Draw(img);d.rectangle((0,0,size[0]-1,size[1]-1),outline=border,width=1)
        img.save(skin/f'library-{kind}-{state}.png')
# Film strips: actual UI assets, not a simulated shader or web-only rendering.
for color,argb in COLORS.items():
    rgb=tuple(bytes.fromhex(argb[2:]))
    for role in ['knob','bipolar']:
        strip=Image.new('RGBA',(112,112*127))
        for i in range(127):
            image=Image.new('RGBA',(448,448));d=ImageDraw.Draw(image)
            d.ellipse((54,58,394,398),fill='#262a38',outline='#454c61',width=4)
            d.arc((25,25,423,423),135,405,fill='#454c61',width=14)
            angle=135+270*i/126
            if role=='bipolar':
                if i!=63:d.arc((25,25,423,423),min(angle,270),max(angle,270),fill=rgb,width=14)
            else:d.arc((25,25,423,423),135,angle,fill=rgb,width=14)
            theta=math.radians(angle);d.line((224+math.cos(theta)*104,224+math.sin(theta)*104,224+math.cos(theta)*151,224+math.sin(theta)*151),fill='#edf0f7',width=12)
            strip.paste(image.resize((112,112),Image.Resampling.LANCZOS),(0,i*112))
        strip.save(skin/f'large-{color}-{role}.png')
    strip=Image.new('RGBA',(280,56*127))
    for i in range(127):
        image=Image.new('RGBA',(560,112));d=ImageDraw.Draw(image);x=24+round(i*512/126)
        d.rounded_rectangle((16,46,544,66),radius=10,fill='#454c61')
        d.rounded_rectangle((16,46,max(36,x),66),radius=10,fill=rgb)
        d.rounded_rectangle((x-18,16,x+18,96),radius=6,fill='#edf0f7')
        strip.paste(image.resize((280,56),Image.Resampling.LANCZOS),(0,i*56))
    strip.save(skin/f'large-{color}-fader.png')
    # 300 px: the slot is 308 wide, so three columns end at x = 996.
    strip=Image.new('RGBA',(300,56*127))
    for i in range(127):
        image=Image.new('RGBA',(600,112));d=ImageDraw.Draw(image);x=26+round(i*548/126)
        d.rounded_rectangle((16,46,584,66),radius=10,fill='#454c61')
        d.rounded_rectangle((16,46,max(36,x),66),radius=10,fill=rgb)
        d.rounded_rectangle((x-18,16,x+18,96),radius=6,fill='#edf0f7')
        strip.paste(image.resize((300,56),Image.Resampling.LANCZOS),(0,i*56))
    strip.save(skin/f'compact-{color}-fader.png')
    for state in ['off','on']:
        img=Image.new('RGBA',(136,72));d=ImageDraw.Draw(img)
        d.rounded_rectangle((2,2,133,69),radius=34,fill=rgb if state=='on' else '#454c61')
        x=100 if state=='on' else 36;d.ellipse((x-25,11,x+25,61),fill='#181a23' if state=='on' else '#edf0f7')
        img.save(skin/f'large-{color}-{state}.png')
        img=Image.new('RGBA',(148,52),('#373045' if state=='on' else '#222531'));d=ImageDraw.Draw(img)
        d.rectangle((0,0,147,51),outline=rgb if state=='on' else '#454c61',width=2 if state=='on' else 1)
        img.save(skin/f'segment-choice-{color}-{state}.png')
    for bank in range(4):
        strip=Image.new('RGBA',(8,64*256))
        for a in range(bank*8,bank*8+8):
            for b in range(32):
                image=Image.new('RGBA',(64,512));d=ImageDraw.Draw(image);d.line((0,(1-a/31)*511,63,(1-b/31)*511),fill=rgb,width=8)
                strip.paste(image.resize((8,64),Image.Resampling.LANCZOS),(0,((a%8)*32+b)*64))
        strip.save(skin/f'segment-{color}-{bank}.png')
# Force slot artwork (0.3.28): the base images are drawn at MPC size and then
# resampled to the Force slot metrics, so a wide card is filled by a bigger
# knob/fader/switch instead of a stretched one. Base names stay untouched.
if FORCE:
    def variant(source,size):
        Image.open(skin/source).resize(size,Image.Resampling.LANCZOS).save(skin/source.replace('.png','-f.png'))
    for color in COLORS:
        for role in ['knob','bipolar']:variant(f'large-{color}-{role}.png',(KNOB,KNOB*127))
        variant(f'compact-{color}-fader.png',(FADER_W,FADER_H*127))
        for state in ['off','on']:
            variant(f'large-{color}-{state}.png',(TOGGLE_W,TOGGLE_H))
            variant(f'segment-choice-{color}-{state}.png',(SEG_W,SEG_H))
            variant(f'large-fx-{state}.png',(TOGGLE_W,TOGGLE_H))
(root/'surface-pages.json').write_text(json.dumps([{k:p[k] for k in ['family','sub','title','items','chart','color','subtitle','sections']} for p in pages],indent=2))
strip=Image.new('RGBA',(1216,4*512))
for frame in range(512):
    d=ImageDraw.Draw(strip);x=round(frame*1213/511);d.rectangle((x,frame*4,x+2,frame*4+3),fill='#edf0f7')
strip.save(skin/'lfo-playhead.png')

# Hardware evidence (upstream PR #5): even a bound, non-focusable Label consumes
# taps over a Button. Render fixed captions INTO each button image, then remove
# the overlay widgets. The custom preset browser is retired; use native VST programs.
# No touch routing assumption or transparent-label workaround is required.
caption_assets={}
def bake_button_captions(entries):
    remove=set()
    for entry in entries:
        model=dict(dict(entry)['componentData'])
        if model['type']!='Button':continue
        data=model['data'];bx,by,bw,bh=map(int,dict(dict(entry)['bounds'])['bounds'].split())
        captions=[]
        for candidate in entries:
            m=dict(dict(candidate)['componentData'])
            if m['type']!='Label':continue
            x,y,w,h=map(int,dict(dict(candidate)['bounds'])['bounds'].split())
            if min(x+w,bx+bw)<=max(x,bx) or min(y+h,by+bh)<=max(y,by):continue
            assert x>=bx and y>=by and x+w<=bx+bw and y+h<=by+bh,('Partial label overlap',model['name'],m['name'])
            d=dict(m['data']);style=json.loads(g.dump_json(d['textStyle']))
            bound=d['handleName'];remap={dict(v)['key']:dict(v)['value'] for v in dict(dict(candidate)['handle remapping'])['map']};bound=remap.get(bound,bound)
            constant=re.search(r'WithName\[([^]]*)\]',bound)
            if d['type']=='Name' and constant:texts=[constant[1],constant[1]]
            elif d['type']=='Value' and bound==dict(data)['handleName']:texts=['Off','On']
            else:raise ValueError('Dynamic label must sit beside button: '+m['name'])
            captions.append({'text':texts,'bounds':[x-bx,y-by,w,h],'style':style});remove.add(id(candidate))
        if not captions:continue
        current=dict(data);signature=json.dumps({'on':current['onImage'],'off':current['offImage'],'captions':captions},sort_keys=True)
        key=hashlib.sha256(signature.encode()).hexdigest()[:16]
        for state,field in enumerate(['offImage','onImage']):
            image=Image.open(skin/current[field]).convert('RGBA');draw=ImageDraw.Draw(image)
            for caption in captions:
                x,y,w,h=caption['bounds'];style=caption['style'];text=caption['text'][state]
                size=int(style['font']['height']);font=ImageFont.truetype(str(root/'fonts/Roboto-Regular.ttf'),size)
                while draw.textlength(text,font=font)>w-4 and size>14:
                    size-=1;font=ImageFont.truetype(str(root/'fonts/Roboto-Regular.ttf'),size)
                assert draw.textlength(text,font=font)<=w,(model['name'],text,w)
                tx=x+(w-draw.textlength(text,font=font))/2 if 'horizontallyCentred' in style['justification'] else x
                bounds=draw.textbbox((0,0),text,font=font);ty=y+(h-(bounds[3]-bounds[1]))/2-bounds[1]
                draw.text((tx,ty),text,font=font,fill='#'+style['colour'][2:])
            filename=f'caption-{key}-{state}.png';image.save(skin/filename);current[field]=filename
            caption_assets[filename]={'size':list(image.size),'texts':[c['text'][state] for c in captions]}
        if isinstance(data,dict):data.update(current)
        else:data[:]=list(current.items())
    entries[:]=[entry for entry in entries if id(entry) not in remove]

for definition in definitions:bake_button_captions(dict(dict(definition)['value'])['componentsData'])
for tab in tabs:bake_button_captions(dict(tab['componentDefinition'])['componentsData'])
# Resolve every direct native widget through an explicit scoped alias. This is
# also used for Local panel buttons and fixed captions; modifiers live in the
# remap value, never in an unregistered widget-local name.
def bind_native_handles(entries):
    for entry in entries:
        model=dict(dict(entry)['componentData']);data=model['data'];fields=dict(data)
        mapping=dict(dict(entry)['handle remapping'])['map'];known={dict(x)['key'] for x in mapping}
        for field,value in list(fields.items()):
            if not isinstance(value,str) or not (field=='handleName' or field.endswith('Handle') or field=='dataHandle'):continue
            if 'Parameter ' not in value and not value.startswith('Local/'):continue
            alias='Bound'+field
            assert alias not in known
            mapping.append(g.obj(('key',alias),('value',value)));known.add(alias);fields[field]=alias
        # Never rebuild the widget from a dict: KnobOverlay/MenuOverlay carry two
        # "version" keys on purpose (ui-reference 6.10/6.11). Collapsing them made
        # the menu overlay open invisibly on the MPC (0.3.2). Replace values in place.
        if isinstance(data,dict):data.update(fields)
        else:data[:]=[(key,fields[key] if key!='version' else value) for key,value in data]
for definition in definitions:bind_native_handles(dict(dict(definition)['value'])['componentsData'])
for tab in tabs:bind_native_handles(dict(tab['componentDefinition'])['componentsData'])
(root/'button-captions.json').write_text(json.dumps(caption_assets,indent=2),encoding='utf-8')
# One design, two skins (0.3.28). The MPC keeps the 1000 px safe area; the Force
# skin is generated at its own width and with its own slot metrics. The firmware
# prefers "TUI - Force.json" over "TUI.json" (ui-reference 8.4), so an MPC still
# loads the untouched layout.
name='TUI - Force.json' if FORCE else 'TUI.json'
(skin/name).write_text(g.dump_json(tui)+'\n',encoding='utf-8')
(skin/('Q-Links - Force.json' if FORCE else 'Q-Links.json')).write_text(g.dump_json(qlinks)+'\n',encoding='utf-8')
print('Generated',len(tabs),'native pages;',len(params),'parameters;',len(replacements),'choice labels;',name,'right edge',SCREEN_RIGHT)

