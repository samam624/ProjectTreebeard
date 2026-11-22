"""Check Altium's saved-object readback and generate a dimension preview.

The compile gate is intentionally separate: a valid binary/library link does not
prove that the application compiler produced a correct connected netlist.
"""
from pathlib import Path
import csv, hashlib, html, json

root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'library-manifest.json').read_text())
audit=json.loads((root/'AuditGeometry-report.json').read_text())
symbols={c['name']:c for f in audit if f['kind']=='SCHLIB' for c in f['components']}
footprints={c['name']:c for f in audit if f['kind']=='PCBLIB' for c in f['components']}
models={m['symbol']:m for m in json.loads((root/'VerifyModels-report.json').read_text())}
bom={r['Library Reference']:r for r in csv.DictReader((root/'placed-bom.csv').open())}

# Independent electrical pin table transcribed from manufacturer pinouts.
expected_pins={
 'TPS70933DBVR':{'1':'IN','2':'GND','3':'EN','4':'NC','5':'OUT'},
 'AO3400A':{'1':'G','2':'S','3':'D'},
 'SS34-E3_57T':{'1':'K','2':'A'},
 'BA4AAPC_LAYOUT_CHECK':{'1':'LOWER_CONTACT','2':'UPPER_CONTACT'},
 'C_RADIAL_D10_P5':{'1':'+','2':'-'},
}
checks=[]
for p in manifest:
 s=symbols[p['symbol']]; f=footprints[p['fp']]
 pins={x['number']:x['name'] for x in s['pin_details']}
 pads={x['name']:x for x in f['pad_details'] if x['name']}
 assert pins==expected_pins[p['symbol']],(p['symbol'],'pin names')
 assert set(pins)==set(pads),(p['symbol'],'pin-pad numbering')
 for n,x,y,w,h,drill in p['pads']:
  for field,target in zip(['x','y','w','h','drill'],[x,y,w,h,drill]):
   assert abs(pads[n][field]-target)<0.00001,(p['fp'],n,field)
  assert pads[n]['layer']==(74 if drill else 1)
  if drill: assert pads[n]['plated']
 holes=[x for x in f['pad_details'] if not x['name']]
 assert len(holes)==len(p.get('holes',[]))
 for x,y,d in p.get('holes',[]):
  assert any(abs(v['x']-x)<.00001 and abs(v['y']-y)<.00001 and abs(v['drill']-d)<.00001 and not v['plated'] for v in holes)
 assert f['bodies']==1
 assert models[p['symbol']]['count']==1 and models[p['symbol']]['first_model']==p['fp']
 row=bom[p['symbol']]
 assert row['Manufacturer Part Number']==p.get('mpn',p['symbol'])
 assert row['LCSC Part #']==p['lcsc'] and row['Footprint']==p['fp']
 checks.append({'symbol':p['symbol'],'pin_pad_geometry_model_bom':'PASS',
   'physical_release':'sample orientation/polarity check required' if p['prefix']=='BT' else ('source to envelope' if p['prefix']=='C' else 'datasheet geometry checked')})

compile_report=json.loads((root/'CompileSavedTest-report.json').read_text())
netrows=list(csv.DictReader((root/'compiled-pin-netlist.csv').open()))
expected_connections={(r['Designator'],n) for symbol,r in bom.items() for n in expected_pins[symbol]}
actual_connections={(r['Designator'],r['Pin']) for r in netrows}
netlist_pass=bool(compile_report['compile_ok'] and actual_connections==expected_connections and
 all(r['Net']==r['Designator']+'_P'+r['Pin'] for r in netrows))
report={'date':'2026-11-11','components':checks,'compiled_connectivity_pass':netlist_pass,
        'compile_report':compile_report,'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('Pump_Controller.*')},
        'release_status':'PLANNING ONLY; holder sample check still required' if netlist_pass else 'PLANNING ONLY; compiled connectivity check incomplete; holder sample check required'}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')

svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="1000" viewBox="0 0 1200 1000">',
 '<rect width="1200" height="1000" fill="#fafafa"/>',
 '<style>text{font-family:Arial,sans-serif;fill:#152635}.title{font-size:23px;font-weight:bold}.small{font-size:14px}</style>',
 '<text x="30" y="35" class="title">Pump controller: saved Altium footprint geometry</text>',
 '<text x="30" y="60" class="small">Component-side view. Each panel has its own scale. Yellow = copper; black = holes; blue = body envelope.</text>']
for i,p in enumerate(manifest):
 col=i%3; row=i//3; cx=200+400*col; cy=285+450*row
 w,h,z=p['body']; a,b,c,d=p['courtyard']; scale=min(300/(c-a),260/(d-b))
 title=p.get('mpn',p['symbol']) or 'Generic radial electrolytic'
 svg += [f'<text x="{cx-170}" y="{cy-165}" class="title">{html.escape(title)}</text>',
         f'<text x="{cx-170}" y="{cy-142}" class="small">Body: {w} × {h} mm; height ≤ {z} mm</text>']
 def box(x,y,w,h,color,stroke='none'):
  return f'<rect x="{cx+(x-w/2)*scale}" y="{cy-(y+h/2)*scale}" width="{w*scale}" height="{h*scale}" fill="{color}" stroke="{stroke}"/>'
 svg += [box(0,0,w,h,'#dce9f2','#59768b')]
 for pad in footprints[p['fp']]['pad_details']:
  x,y,pw,ph,dr= [pad[k] for k in ['x','y','w','h','drill']]
  if pad['name']: svg += [box(x,y,pw,ph,'#d7ac3a')]
  if dr: svg += [f'<circle cx="{cx+x*scale}" cy="{cy-y*scale}" r="{dr*scale/2}" fill="#161e25"/>']
  if pad['name']: svg += [f'<text x="{cx+x*scale}" y="{cy-y*scale+5}" text-anchor="middle" font-size="15" fill="#111">{html.escape(pad["name"])}</text>']
 svg += [f'<text x="{cx-170}" y="{cy+155}" class="small">{html.escape(p["fp"])}</text>']
 if p['prefix']=='BT': svg += [f'<text x="{cx-170}" y="{cy+178}" class="small">1 / 2 are neutral contact IDs: confirm polarity.</text>']
 if p['prefix']=='C': svg += [f'<text x="{cx-170}" y="{cy+178}" class="small">5 mm lead pitch. Pad 1 = positive.</text>']
svg += ['</svg>']
(root/'footprint-preview.svg').write_text('\n'.join(svg),encoding='utf8')
print('PASS: five saved symbols/footprints, dimensions, model links and placed BOM fields.')
print('Compiled connectivity:', 'PASS' if netlist_pass else 'INCOMPLETE (see validation.json)')
