import json, re, subprocess, sys, xml.etree.ElementTree as ET
from pathlib import Path
root=Path(__file__).resolve().parent.parent
sources=set()
for path in (root/'src').glob('*.cpp'):
    for match in re.finditer(r'\btr\(\s*("(?:\\.|[^"\\])*")\s*\)',path.read_text(encoding='utf-8')):
        sources.add(json.loads(match[1]))
translations={}
for line in (root/'translations/zh_CN.map').read_text(encoding='utf-8').splitlines():
    if not line or '|' not in line: continue
    source,target=line.split('|',1);target=target.replace('\\n','\n')
    if source.startswith('@'):
        matches=[value for value in sources if value.startswith(source[1:])]
        if len(matches)!=1: raise ValueError((source,matches))
        source=matches[0]
    translations[source]=target
missing=sources-translations.keys()
if missing: raise ValueError('Untranslated strings: '+repr(sorted(missing)))
for source,target in translations.items():
    if sorted(re.findall(r'%[1-9n]',source))!=sorted(re.findall(r'%[1-9n]',target)):raise ValueError('Placeholder mismatch: '+source)
ts=ET.Element('TS',version='2.1',language='zh_CN',sourcelanguage='en');context=ET.SubElement(ts,'context');ET.SubElement(context,'name').text='SameBoyQt'
for source,target in sorted(translations.items()):
    message=ET.SubElement(context,'message');ET.SubElement(message,'source').text=source;ET.SubElement(message,'translation').text=target
ET.indent(ts);ET.ElementTree(ts).write(root/'translations/zh_CN.ts',encoding='utf-8',xml_declaration=True)
if len(sys.argv)>1: subprocess.run([sys.argv[1],str(root/'translations/zh_CN.ts'),'-qm',str(root/'translations/zh_CN.qm')],check=True)
print(f'{len(translations)} translations; all {len(sources)} literal tr() strings covered')
