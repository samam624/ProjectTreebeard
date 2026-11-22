from pathlib import Path
p=Path(__file__).parent
s=(p/'AuditExisting.pas').read_text()
extra='''
function PcbPads(Comp : IPCB_LibComponent) : String;
var Iter : IPCB_GroupIterator; Pad : IPCB_Pad; First : Boolean;
begin
 Result := '['; First := True;
 Iter := Comp.GroupIterator_Create; Iter.AddFilter_ObjectSet(MkSet(ePadObject));
 Pad := Iter.FirstPCBObject;
 while Pad <> nil do begin
  if not First then Result := Result+','; First := False;
  Result := Result+'{"name":"'+JsonEscape(Pad.Name)+'","x":'+FloatToStr((Pad.X-Comp.X)/393700.7874015748)+
   ',"y":'+FloatToStr((Pad.Y-Comp.Y)/393700.7874015748)+
   ',"w":'+FloatToStr(Pad.TopXSize/393700.7874015748)+',"h":'+FloatToStr(Pad.TopYSize/393700.7874015748)+
   ',"drill":'+FloatToStr(Pad.HoleSize/393700.7874015748)+',"layer":'+IntToStr(Pad.Layer);
  if Pad.Plated then Result := Result+',"plated":true}' else Result := Result+',"plated":false}';
  Pad := Iter.NextPCBObject;
 end;
 Comp.GroupIterator_Destroy(Iter); Result := Result+']';
end;

function SchPins(Comp : ISch_Component) : String;
var Iter : ISch_Iterator; Pin : ISch_Pin; First : Boolean;
begin
 Result := '['; First := True;
 Iter := Comp.SchIterator_Create; Iter.AddFilter_ObjectSet(MkSet(ePin));
 Pin := Iter.FirstSchObject;
 while Pin <> nil do begin
  if not First then Result := Result+','; First := False;
  Result := Result+'{"number":"'+JsonEscape(Pin.Designator)+'","name":"'+JsonEscape(Pin.Name)+
   '","electrical":'+IntToStr(Pin.Electrical)+'}';
  Pin := Iter.NextSchObject;
 end;
 Comp.SchIterator_Destroy(Iter); Result := Result+']';
end;

function SchParams(Comp : ISch_Component) : String;
var Iter : ISch_Iterator; Param : ISch_Parameter; First : Boolean;
begin
 Result := '{'; First := True;
 Iter := Comp.SchIterator_Create; Iter.AddFilter_ObjectSet(MkSet(eParameter));
 Param := Iter.FirstSchObject;
 while Param <> nil do begin
  if not First then Result := Result+','; First := False;
  Result := Result+'"'+JsonEscape(Param.Name)+'":"'+JsonEscape(Param.Text)+'"';
  Param := Iter.NextSchObject;
 end;
 Comp.SchIterator_Destroy(Iter); Result := Result+'}';
end;
'''
s=s.replace('function PcbDetails : String;',extra+'\nfunction PcbDetails : String;')
s=s.replace("',\"pads\":' + IntToStr(CountPcbKind(Comp, ePadObject)) +", "',\"pad_details\":' + PcbPads(Comp) + ',\"pads\":' + IntToStr(CountPcbKind(Comp, ePadObject)) +")
s=s.replace("',\"pins\":' + IntToStr(CountSchKind(Comp, ePin)) +", "',\"pin_details\":' + SchPins(Comp) + ',\"params\":' + SchParams(Comp) + ',\"pins\":' + IntToStr(CountSchKind(Comp, ePin)) +")
(p/'AuditGeometry.pas').write_text(s)
