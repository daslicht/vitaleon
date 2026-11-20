"""Build integration checks. Optional: --presets path/to/your/Vital/library."""
from pathlib import Path
import argparse,subprocess,shutil,sys,os,json,zipfile
parser=argparse.ArgumentParser();parser.add_argument('--presets',default='');args=parser.parse_args()
project=Path(__file__).resolve().parents[1];sdk=Path(__import__('os').environ.get('MPC_SDK',str(project.parents[1])));src=project/'src'
out=project/'build/full-checks';out.mkdir(parents=True,exist_ok=True)
compiler=shutil.which('clang++') or shutil.which('g++')
if not compiler:raise SystemExit('A C++17 clang++ or g++ compiler is required.')
exe=out/('full_checks.exe' if sys.platform=='win32' else 'full_checks')
with (out/'build.log').open('w') as log:
    result=subprocess.run([compiler,'-std=c++17','-O2','-I'+str(sdk/'framework'),str(project/'tests/full_checks.cpp'),*[str(src/f) for f in ['Engine.cpp','Pffft.cpp','PffftCommon.cpp','WavetableImport.cpp','BankInflate.cpp']],'-o',str(exe)],stdout=log,stderr=subprocess.STDOUT)
if result.returncode:print((out/'build.log').read_text());raise SystemExit(result.returncode)
fixture=out/'test-content';presets=fixture/'Content/Presets';banks=fixture/'Content/Banks';presets.mkdir(parents=True,exist_ok=True);banks.mkdir(parents=True,exist_ok=True)
def preset(cutoff,**extra):return json.dumps({'synth_version':'1.0.6','settings':{'filter_1_cutoff':cutoff,'volume':3136,**extra}})
(presets/'01 Import Test.vital').write_text(preset(60))
for n in range(4,11):(presets/f'{n:02} Browser Fixture.vital').write_text(preset(40+n))
(presets/'03 Unsupported.vital').write_text(preset(99,filter_1_model=99))
with zipfile.ZipFile(banks/'Imported Bank.vitalbank','w',compression=zipfile.ZIP_DEFLATED) as z:z.writestr('Presets/02 Bank Test.vital',preset(77))
with zipfile.ZipFile(banks/'Second Bank.vitalbank','w',compression=zipfile.ZIP_DEFLATED) as z:z.writestr('Presets/02 Bank Test.vital',preset(88))
external=fixture/'Content/Storage Tests';external.mkdir(exist_ok=True)
for name,values in [('A small',[31]),('B large',[71,72,73]),('C same size',[81]),('D empty',[])]:
    with zipfile.ZipFile(external/(name+'.vitalbank'),'w',compression=zipfile.ZIP_DEFLATED) as z:
        for i,value in enumerate(values):z.writestr('Presets/'+('Same name' if i==0 else 'Sound '+str(i))+'.vital',preset(value))
(external/'E broken.vitalbank').write_bytes(b'broken bank')
loose=external/'Loose Sounds';loose.mkdir(exist_ok=True)
(loose/'First loose.vital').write_text(preset(52));(loose/'Second loose.vital').write_text(preset(53,osc_1_spectral_morph_type=14,pitch_env_type=0))
for i in range(8):(external/f'Folder {i}').mkdir(exist_ok=True)
env=os.environ.copy();env['MPC_PLUGIN_FOLDER']=str(fixture)
with (out/'results.log').open('w') as log:
    result=subprocess.run([str(exe),args.presets,str(out/'preview-values.json'),str(fixture),str(project/'skins/TUI.json')],env=env,stdout=log,stderr=subprocess.STDOUT)
    empty=out/'no-content';(empty/'Content').mkdir(parents=True,exist_ok=True);bare=os.environ.copy();bare['MPC_PLUGIN_FOLDER']=str(empty)
    log.flush();second=subprocess.run([str(exe),'--no-content',str(out/'init-values.json')],env=bare,stdout=log,stderr=subprocess.STDOUT)
print((out/'results.log').read_text());raise SystemExit(result.returncode or second.returncode)
