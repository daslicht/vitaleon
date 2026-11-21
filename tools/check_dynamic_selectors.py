"""Selectors filled at run time must not carry a fixed label list in the skin.

The parameter dump (build/params.json) is made on the build computer, with
whatever tables, presets and banks are installed there. Baking those names
into replacementStrings would mislabel the user's own content and fix the
step count (0.3.18: caught before release, the dump had started to include
the 32 factory tables and a two-entry Library Bank).
"""
from pathlib import Path
import json, re
project = Path(__file__).resolve().parents[1]
skin = (project / 'skins/TUI.json').read_text(encoding='utf-8')
params = {p['index']: p['name'] for p in json.loads((project / 'build/params.json').read_text(encoding='utf-8'))['params']}
labelled = {int(m.group(1)): re.sub(r'\s+', '', m.group(2)) for m in re.finditer(r'"key":\s*"Parameter (\d+)",\s*"value":\s*\[([^\]]*)\]', skin)}
bad = []
for index, name in params.items():
    # 'Table Step' is the one table handle with a fixed list: its strings are the
    # synthetic step numbers 1..kTableSteps, not names read from the Content folder.
    dynamic = name == 'Preset' or ('Table' in name and name != 'Table Step') or name.startswith('Library ') or name in ('Sample File', 'LFO Shape File', 'UI Group', 'UI Page', 'UI Section', 'UI Pick')
    if dynamic and index in labelled and labelled[index] != '"Off","On"':
        bad.append((index, name, labelled[index][:60]))
assert not bad, bad
print('PASS: %d labelled parameters, no run-time selector carries a fixed label list' % len(labelled))
