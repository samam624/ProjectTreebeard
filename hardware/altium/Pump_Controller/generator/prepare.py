"""Generate reproducible Altium DelphiScript from the maintained exact-part template."""
from pathlib import Path
import json, re

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT.parent / 'SC-32S_C97604/generator/GenerateSC32SC97604.pas'

# Coordinates are component-side PCB view, millimetres. Mechanical holes are NPTH.
PARTS = [
 dict(symbol='TPS70933DBVR', fp='TB_TPS709_DBV0005A', prefix='U', manufacturer='Texas Instruments', lcsc='C89347',
      desc='3.3V 150mA 30V low-Iq LDO; DBV non-A/B pinout', body=[1.6,2.9,1.45],
      source='TI TPS709 Rev H; DBV0005A drawing 4214839/K 08/2024',
      pins=[['1','IN','Power'],['2','GND','Power'],['3','EN','Input'],['4','NC','Passive'],['5','OUT','Power']],
      pads=[['1',-1.3,.95,1.1,.6,0],['2',-1.3,0,1.1,.6,0],['3',-1.3,-.95,1.1,.6,0],['4',1.3,-.95,1.1,.6,0],['5',1.3,.95,1.1,.6,0]],
      courtyard=[-2.1,-1.75,2.1,1.75], note='EN may float for always-on; do not connect EN directly to 12V.'),
 dict(symbol='AO3400A',fp='TB_AOS_SOT23_PO00001M',prefix='Q',manufacturer='Alpha & Omega Semiconductor',lcsc='C20917',
      desc='30V N-channel MOSFET; G=1 S=2 D=3',body=[2.9,1.6,1.25],
      source='AOS AO3400A Rev 3.1 July 2023; SOT23 PO-00001 version M',
      pins=[['1','G','Input'],['2','S','Passive'],['3','D','Passive']],
      pads=[['1',-.95,-1.2,.8,.8,0],['2',.95,-1.2,.8,.8,0],['3',0,1.2,.8,.8,0]],
      courtyard=[-1.8,-1.85,1.8,1.85], note='48mOhm max at VGS=2.5V; verify motor stall current and PCB thermal design.'),
 dict(symbol='SS34-E3_57T',mpn='SS34-E3/57T',fp='TB_VISHAY_SMC_SS34',prefix='D',manufacturer='Vishay General Semiconductor',lcsc='C13354',
      desc='40V 3A Schottky flyback diode; SMC DO-214AB',body=[7.11,6.22,2.62],
      source='Vishay document 88751 Rev 23-Apr-2020 page 3',
      pins=[['1','K','Passive'],['2','A','Passive']],
      pads=[['1',-3.205,0,1.72,3.2,0],['2',3.205,0,1.72,3.2,0]],
      courtyard=[-4.32,-3.36,4.32,3.36],note='Pad 1 is cathode (band), connected to pump positive. Not an SMA footprint.'),
 dict(symbol='BA4AAPC_LAYOUT_CHECK',mpn='BA4AAPC',fp='TB_MPD_BA4AAPC_REVH',prefix='BT',manufacturer='Memory Protection Devices, Inc.',lcsc='C6748055',
      desc='4xAA PCB holder; layout model; terminal polarity requires sample check',body=[57.48,63.09,15.88],
      source='MPD BA4AAPC drawing Rev H 03/06/19 PCB LAYOUT',
      pins=[['1','LOWER_CONTACT','Passive'],['2','UPPER_CONTACT','Passive']],
      pads=[['1',26.82,-21.135,2.2,2.2,1.02],['2',26.82,-8.435,2.2,2.2,1.02]],
      holes=[[0,-27.305,2.03],[0,27.305,2.03]],courtyard=[-29.24,-32.045,29.24,32.045],
      note='PCB layout orientation and physical terminal polarity must be confirmed against a sample before fabrication. Pins deliberately NOT named +/-.'),
 dict(symbol='C_RADIAL_D10_P5',mpn='',fp='TB_CAP_RADIAL_D10_P5',prefix='C',manufacturer='',lcsc='',
      desc='Generic polarized radial capacitor; D<=10mm P=5mm H<=20mm; 220uF >=25V target',body=[10,10,20],
      source='User-selected lab sourcing envelope; custom radial land pattern, not manufacturer-specific',
      pins=[['1','+','Passive'],['2','-','Passive']],
      pads=[['1',-2.5,0,1.8,1.8,.9],['2',2.5,0,1.8,1.8,.9]],
      courtyard=[-5.5,-5.5,5.5,5.5],note='Source 220uF >=25V, 5mm lead spacing, <=10mm diameter, <=20mm height, leads <=0.6mm. Check leakage for battery life.')
]

def q(s): return "'"+str(s).replace("'","''")+"'"
def nums(seq): return ', '.join(str(x) for x in seq)
def replace_proc(s,name,body):
    return re.sub(r'procedure '+name+r'\b.*?(?=\nprocedure |\nfunction |\Z)',lambda _:body+'\n',s,flags=re.S)

base=TEMPLATE.read_text(encoding='utf-8-sig')
base=base.replace("'C:\\Users\\Public\\treebeard_altium_sc32s\\'", "'__OUTPUT__'")
base=re.sub(r"REQUEST_FILE = '[^']*';", "REQUEST_FILE = '__REQUEST__';",base)
base=re.sub(r"RESPONSE_FILE = '[^']*';", "RESPONSE_FILE = '__RESPONSE__';",base)
base=base.replace("'SC-32S_C97604'", "'Pump_Controller'")
base=base.replace("    SYMBOL_NAME  = 'SC-32S32.768kHz20PPM7pF';\n    FOOTPRINT_NAME = 'SC-32S_SMD3215_2P';",'')
base=replace_proc(base,'AddExtrudedBody', '''procedure AddExtrudedBody(Lib : IPCB_Library; Comp : IPCB_LibComponent; W,H,Z : Double);
var Body : IPCB_ComponentBody; Contour : IPCB_Contour;
begin
    Body := PCBServer.PCBObjectFactory(eComponentBodyObject,eNoDimension,eCreate_Default);
    Body.BodyProjection := eBoardSide_Top;
    Body.Layer := LayerUtils.MechanicalLayer(13);
    Body.StandoffHeight := 0; Body.OverallHeight := Mm(Z);
    Body.BodyColor3D := $606060; Body.BodyOpacity3D := 1.0;
    Contour := Body.MainContour.Replicate; Contour.Count := 4;
    Contour.X[1] := Comp.X-Mm(W/2); Contour.Y[1] := Comp.Y-Mm(H/2);
    Contour.X[2] := Comp.X+Mm(W/2); Contour.Y[2] := Comp.Y-Mm(H/2);
    Contour.X[3] := Comp.X+Mm(W/2); Contour.Y[3] := Comp.Y+Mm(H/2);
    Contour.X[4] := Comp.X-Mm(W/2); Contour.Y[4] := Comp.Y+Mm(H/2);
    Body.SetOutlineContour(Contour); RegisterPcbPrimitive(Lib,Comp,Body);
end;

procedure AddHole(Lib : IPCB_Library; Comp : IPCB_LibComponent; X,Y,Size,Drill : Double; Name : String; Plated : Boolean);
var Pad : IPCB_Pad;
begin
    Pad := PCBServer.PCBObjectFactory(ePadObject,eNoDimension,eCreate_Default);
    Pad.Name := Name; Pad.X := Comp.X+Mm(X); Pad.Y := Comp.Y+Mm(Y);
    Pad.Mode := ePadMode_Simple; Pad.Layer := eMultiLayer;
    Pad.HoleSize := Mm(Drill); Pad.Plated := Plated;
    Pad.TopXSize := Mm(Size); Pad.TopYSize := Mm(Size);
    Pad.MidXSize := Mm(Size); Pad.MidYSize := Mm(Size);
    Pad.BotXSize := Mm(Size); Pad.BotYSize := Mm(Size);
    Pad.TopShape := eRounded; Pad.MidShape := eRounded; Pad.BotShape := eRounded;
    RegisterPcbPrimitive(Lib,Comp,Pad);
end;''')
pcb=['procedure GeneratePcbLib(const OutputDir : String);','var Doc : IServerDocument; Lib : IPCB_Library; Comp,DefaultComp : IPCB_LibComponent;','begin',"Doc := CreateNewDocumentFromDocumentKind('PCBLIB');",'Lib := PCBServer.GetCurrentPCBLibrary; DefaultComp := Lib.CurrentComponent;','PCBServer.PreProcess;','try']
for p in PARTS:
    pcb += ['Comp := PCBServer.CreatePCBLibComp;',f'Comp.Name := {q(p["fp"])};',f'Comp.Description := {q(p["desc"])};','Lib.RegisterComponent(Comp); Lib.CurrentComponent := Comp;']
    for n,x,y,w,h,d in p['pads']:
        if d: pcb += [f'AddHole(Lib,Comp,{nums([x,y,w,d])},{q(n)},True);']
        else: pcb += [f'AddSmdPad(Lib,Comp,{nums([x,y,w,h])},{q(n)});']
    for x,y,d in p.get('holes',[]): pcb += [f"AddHole(Lib,Comp,{nums([x,y,d,d])},'',False);"]
    w,h,z=p['body']
    pcb += [f'AddPcbBox(Lib,Comp,{nums(p["courtyard"])},0.05,eMechanical1);',f'AddPcbBox(Lib,Comp,{-w/2},{-h/2},{w/2},{h/2},0.1,eMechanical13);',f'AddExtrudedBody(Lib,Comp,{w},{h},{z});']
    # Silk along pad-free body sides; explicit pin-1 marker outside pad/courtyard.
    if p['symbol']=='TPS70933DBVR': silk=[[-.55,-1.5,.55,-1.5],[-.55,1.5,.55,1.5],[-2,1.45,-1.7,1.45]]
    elif p['symbol']=='AO3400A': silk=[[-1.6,-.65,-1.6,.65],[1.6,-.65,1.6,.65],[-1.3,-1.9,-.6,-1.9]]
    elif p['prefix']=='D': silk=[[-3.6,-3.25,3.6,-3.25],[-3.6,3.25,3.6,3.25],[-3.6,-3.25,-3.6,-1.9],[-3.6,1.9,-3.6,3.25]]
    else: silk=[[-w/2-.15,-h/2-.15,w/2+.15,-h/2-.15],[-w/2-.15,h/2+.15,w/2+.15,h/2+.15],[-w/2-.15,-h/2-.15,-w/2-.15,h/2+.15],[w/2+.15,-h/2-.15,w/2+.15,h/2+.15]]
    if p['prefix']=='C': silk += [[-4,1.5,-2.5,1.5],[-3.25,.75,-3.25,2.25]]
    for line in silk: pcb += [f'AddPcbTrack(Lib,Comp,{nums(line)},0.12,eTopOverlay);']
pcb += ['finally PCBServer.PostProcess; end;','if DefaultComp <> nil then begin Lib.DeRegisterComponent(DefaultComp); Lib.RemoveComponent(DefaultComp); end;',"Doc.SetModified(True); Doc.DoSafeChangeFileNameAndSave(OutputDir+LIB_BASENAME+'.PcbLib','PCBLIB');",'end;']
base=replace_proc(base,'GeneratePcbLib','\n'.join(pcb))
base=base.replace('Orientation : TRotationBy90; Designator : String);','Orientation : TRotationBy90; Designator, PinName : String; Electrical : Integer);')
base=base.replace('Pin.Electrical           := eElectricPassive;', 'Pin.Electrical           := Electrical;')
base=base.replace("Pin.Name                 := '';",'Pin.Name                 := PinName;').replace('Pin.ShowDesignator       := False;','Pin.ShowDesignator       := True;').replace('Pin.ShowName             := False;','Pin.ShowName             := True;')
base=base.replace('procedure AddFootprint(Comp : ISch_Component);','procedure AddFootprint(Comp : ISch_Component; FootprintName : String);').replace('FOOTPRINT_NAME','FootprintName')
base=base.replace("'Seiko SC-32S manufacturer recommended 3215 two-pad footprint'","'Verified source-linked PCB model; see Source and Assembly Notes parameters'")
sch=['procedure GenerateSchLib(const OutputDir : String);','var Doc : IServerDocument; Lib : ISch_Lib; Comp,DefaultComp : ISch_Component;','begin',"Doc := CreateNewDocumentFromDocumentKind('SCHLIB');",'Lib := SchServer.GetCurrentSchDocument; DefaultComp := Lib.CurrentSchComponent;']
for p in PARTS:
    sch += ['Comp := SchServer.SchObjectFactory(eSchComponent,eCreate_Default);',f'Comp.LibReference := {q(p["symbol"])};',f'Comp.Designator.Text := {q(p["prefix"]+"?")};',f'Comp.ComponentDescription := {q(p["desc"])};', 'Comp.PartCount := 1; Comp.CurrentPartId := 1; Comp.DisplayMode := 0;','Lib.AddSchComponent(Comp);',f'Comp.LibReference := {q(p["symbol"])};',f'Comp.Comment.Text := {q(p.get("mpn",p["symbol"]) or "220uF >=25V")};','Comp.Designator.IsHidden := False; Comp.Comment.IsHidden := False;','SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress,c_BroadCast,SCHM_PrimitiveRegistration,Comp.I_ObjectAddress);','Lib.CurrentSchComponent := Comp;']
    # Clearly named pins; standard passive graphics for capacitor and Schottky diode.
    if p['prefix'] in ['C','D']:
        sch += [f"AddPin(Comp,100,0,100,eRotate180,'1',{q(p['pins'][0][1])},eElectricPassive);",f"AddPin(Comp,300,0,100,eRotate0,'2',{q(p['pins'][1][1])},eElectricPassive);"]
        if p['prefix']=='C': lines=[[100,0,180,0],[180,-80,180,80],[220,-80,220,80],[220,0,300,0],[125,60,155,60],[140,45,140,75]]
        else: lines=[[100,0,170,0],[170,-70,170,70],[170,70,190,70],[170,-70,150,-70],[170,0,250,65],[250,65,250,-65],[250,-65,170,0],[250,0,300,0]]
        for line in lines: sch += [f'AddSchLine(Comp,{nums(line)});']
    elif p['prefix']=='Q':
        sch += ["AddPin(Comp,100,0,100,eRotate180,'1','G',eElectricInput);","AddPin(Comp,300,-100,100,eRotate0,'2','S',eElectricPassive);","AddPin(Comp,300,100,100,eRotate0,'3','D',eElectricPassive);"]
        for line in [[100,0,160,0],[160,-60,160,60],[180,-65,180,-35],[180,-15,180,15],[180,35,180,65],[180,50,230,50],[230,50,230,100],[230,100,300,100],[180,-50,230,-50],[230,-50,230,-100],[230,-100,300,-100],[230,-50,230,0],[230,0,180,0],[180,0,200,10],[180,0,200,-10]]:
            sch += [f'AddSchLine(Comp,{nums(line)});']
    else:
        sch += [f'AddSchRect(Comp,100,{-len(p["pins"])*100},500,100);']
        for i,(n,name,etype) in enumerate(p['pins']): sch += [f'AddPin(Comp,100,{-i*100},100,eRotate180,{q(n)},{q(name)},eElectric{etype});']
    metadata={'Manufacturer':p['manufacturer'],'Manufacturer Part Number':p.get('mpn',p['symbol']),'LCSC Part #':p['lcsc'],'Do Not Substitute':'No - envelope constrained' if p['prefix']=='C' else 'Yes','Footprint':p['fp'],'Source':p['source'],'Assembly Notes':p['note'],'Sourcing Status':'Lab sourced; verify body and pitch' if p['prefix']=='C' else ('LCSC out of stock 2026-11-11; alternate source Digi-Key 2439225, availability recheck required' if p['prefix']=='BT' else 'LCSC in stock verified 2026-11-11; recheck at order time'),'Verification Status':'Layout only - polarity/sample check required' if p['prefix']=='BT' else 'See validation report'}
    for k,v in metadata.items(): sch += [f'AddParameter(Comp,{q(k)},{q(v)});']
    sch += [f'AddFootprint(Comp,{q(p["fp"])});']
sch += ['if DefaultComp <> nil then Lib.RemoveSchComponent(DefaultComp);','Lib.GraphicallyInvalidate; Doc.SetModified(True);',"Doc.DoSafeChangeFileNameAndSave(OutputDir+LIB_BASENAME+'.SchLib','SCHLIB');",'end;']
base=replace_proc(base,'GenerateSchLib','\n'.join(sch))
base=base.replace('OutputDir := ReadOutputDir;',"OutputDir := '__OUTPUT__';")
base=base.replace('SC-32S_C97604.IntLib','Pump_Controller.IntLib').replace('SC-32S library generation','pump library generation')
(ROOT/'generator/GeneratePump.pas').write_text(base,encoding='utf8')
(ROOT/'library-manifest.json').write_text(json.dumps(PARTS,indent=2)+'\n',encoding='utf8')
print('Prepared',len(PARTS),'components')
