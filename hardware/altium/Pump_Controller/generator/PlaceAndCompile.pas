{ Isolated library test sheet; NOT the pump controller circuit. }
const OUTPUT_DIR = '__OUTPUT__'; RESPONSE_FILE = '__RESPONSE__';

function Csv(S : String) : String;
begin
 Result := '"'+StringReplace(S,'"','""',MkSet(rfReplaceAll))+'"';
end;

procedure PlacePart(Symbol : String; X,Y : Integer);
begin
 IntegratedLibraryManager.PlaceLibraryComponent(Symbol,OUTPUT_DIR+'Pump_Controller.IntLib',
  'Orientation=0|Location.X='+IntToStr(MilsToCoord(X))+'|Location.Y='+IntToStr(MilsToCoord(Y)));
end;

procedure Run;
var
 Doc : IServerDocument; Sheet : ISch_Document;
 Iter,SubIter : ISch_Iterator; Comp : ISch_Component; Pin : ISch_Pin;
 Param : ISch_Parameter; LabelObj : ISch_NetLabel;
 Lines,Bom,Netlist,Response : TStringList;
 Workspace : IWorkspace; Project : IProject; Flat : IDocument;
 Net : INet; NetItem : INetItem;
 I,K,Count : Integer; Ref,MPN,LCSC,FP,Source,Prefix : String;
begin
 Lines := TStringList.Create; Bom := TStringList.Create;
 Netlist := TStringList.Create; Response := TStringList.Create;
 try
  Doc := CreateNewDocumentFromDocumentKind('SCH');
  Client.ShowDocument(Doc); Sheet := SchServer.GetCurrentSchDocument;
  PlacePart('TPS70933DBVR',1000,3000);
  PlacePart('AO3400A',2500,3000);
  PlacePart('SS34-E3_57T',4000,3000);
  PlacePart('BA4AAPC_LAYOUT_CHECK',1000,1500);
  PlacePart('C_RADIAL_D10_P5',3000,1500);
  Bom.Add('Designator,Library Reference,Manufacturer Part Number,LCSC Part #,Footprint,Sourcing Status');
  Count := 0;
  Iter := Sheet.SchIterator_Create; Iter.AddFilter_ObjectSet(MkSet(eSchComponent));
  Comp := Iter.FirstSchObject;
  while Comp <> nil do begin
   Inc(Count); Ref := 'X'+IntToStr(Count); Comp.Designator.Text := Ref;
   MPN := ''; LCSC := ''; FP := ''; Source := '';
   SubIter := Comp.SchIterator_Create; SubIter.AddFilter_ObjectSet(MkSet(eParameter));
   Param := SubIter.FirstSchObject;
   while Param <> nil do begin
    if Param.Name='Manufacturer Part Number' then MPN := Param.Text;
    if Param.Name='LCSC Part #' then LCSC := Param.Text;
    if Param.Name='Footprint' then FP := Param.Text;
    if Param.Name='Sourcing Status' then Source := Param.Text;
    Param := SubIter.NextSchObject;
   end;
   Comp.SchIterator_Destroy(SubIter);
   Bom.Add(Csv(Ref)+','+Csv(Comp.LibReference)+','+Csv(MPN)+','+Csv(LCSC)+','+Csv(FP)+','+Csv(Source));
   SubIter := Comp.SchIterator_Create; SubIter.AddFilter_ObjectSet(MkSet(ePin));
   Pin := SubIter.FirstSchObject;
   while Pin <> nil do begin
    LabelObj := SchServer.SchObjectFactory(eNetLabel,eCreate_Default);
    LabelObj.Location := Pin.Location;
    LabelObj.Text := Ref+'_P'+Pin.Designator;
    Sheet.RegisterSchObjectInContainer(LabelObj);
    SchServer.RobotManager.SendMessage(Sheet.I_ObjectAddress,c_BroadCast,SCHM_PrimitiveRegistration,LabelObj.I_ObjectAddress);
    Pin := SubIter.NextSchObject;
   end;
   Comp.SchIterator_Destroy(SubIter);
   Comp := Iter.NextSchObject;
  end;
  Sheet.SchIterator_Destroy(Iter);
  Sheet.GraphicallyInvalidate;
  Doc.SetModified(True); Doc.SetFileName(OUTPUT_DIR+'Library_Test.SchDoc'); Doc.DoFileSave('');
  if not FileExists(OUTPUT_DIR+'Library_Test.SchDoc') then begin
   Response.Text := '{"error":"test sheet save failed"}'; Response.SaveToFile(RESPONSE_FILE); Exit;
  end;
  Bom.SaveToFile(OUTPUT_DIR+'placed-bom.csv');
  Lines.Add('[Design]'); Lines.Add('Version=1.0'); Lines.Add('HierarchyMode=0');
  Lines.Add('[Document1]'); Lines.Add('DocumentPath=Library_Test.SchDoc'); Lines.Add('AnnotationEnabled=1');
  Lines.SaveToFile(OUTPUT_DIR+'Library_Test.PrjPcb');
  Workspace := GetWorkspace;
  Project := Workspace.DM_OpenProject(OUTPUT_DIR+'Library_Test.PrjPcb',False);
  Project.DM_AddSourceDocument(OUTPUT_DIR+'Library_Test.SchDoc');
  Project.DM_Compile;
  Flat := Project.DM_DocumentFlattened;
  if Flat = nil then Flat := Project.DM_LogicalDocuments(0);
  Netlist.Add('Net,Designator,Pin');
  for I := 0 to Flat.DM_NetCount-1 do begin
   Net := Flat.DM_Nets(I);
   for K := 0 to Net.DM_PinCount-1 do begin
    NetItem := Net.DM_Pins(K);
    Netlist.Add(Csv(Net.DM_NetName)+','+Csv(NetItem.DM_LogicalPartDesignator)+','+Csv(NetItem.DM_PinNumber));
   end;
  end;
  Netlist.SaveToFile(OUTPUT_DIR+'compiled-pin-netlist.csv');
  Response.Text := '{"placed_components":'+IntToStr(Count)+',"compiled_components":'+IntToStr(Flat.DM_ComponentCount)+',"nets":'+IntToStr(Flat.DM_NetCount)+',"pin_net_rows":'+IntToStr(Netlist.Count-1)+'}';
  Response.SaveToFile(RESPONSE_FILE);
 finally Lines.Free; Bom.Free; Netlist.Free; Response.Free; end;
end;
