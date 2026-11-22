const OUTPUT_DIR = '__OUTPUT__'; RESPONSE_FILE = '__RESPONSE__';
procedure Run;
var W : IWorkspace; P : IProject; D : IDocument; N : INet; Item : INetItem; SD : IServerDocument; V : IViolation;
    R,L : TStringList; I,J,K : Integer; Ok : Boolean; Json : String;
begin
 R := TStringList.Create; L := TStringList.Create;
 try
  W := GetWorkspace; P := W.DM_OpenProject(OUTPUT_DIR+'Library_Test.PrjPcb',False);
  SD := Client.OpenDocument('SCH',OUTPUT_DIR+'Library_Test.SchDoc'); Client.ShowDocument(SD);
  P.DM_SetAsCurrentProject;
  ResetParameters; AddStringParameter('Action','Recompile');
  AddStringParameter('ObjectKind','Project'); RunProcess('WorkspaceManager:Compile');
  Ok := P.DM_Compile;
  Json := '{"compile_ok":'; if Ok then Json := Json+'true' else Json := Json+'false';
  Json := Json+',"logical_documents":'+IntToStr(P.DM_LogicalDocumentCount)+',"documents":[';
  L.Add('Net,Designator,Pin');
  for I := 0 to P.DM_LogicalDocumentCount-1 do begin
   D := P.DM_LogicalDocuments(I); D.DM_Compile;
   if I>0 then Json := Json+',';
   Json := Json+'{"name":"'+ExtractFileName(D.DM_FullPath)+'","components":'+IntToStr(D.DM_ComponentCount)+',"nets":'+IntToStr(D.DM_NetCount)+'}';
   for J := 0 to D.DM_NetCount-1 do begin
    N := D.DM_Nets(J);
    for K := 0 to N.DM_PinCount-1 do begin
     Item := N.DM_Pins(K); L.Add(N.DM_NetName+','+Item.DM_LogicalPartDesignator+','+Item.DM_PinNumber);
    end;
   end;
  end;
  L.SaveToFile(OUTPUT_DIR+'compiled-pin-netlist.csv');
  Json := Json+'],"pin_rows":'+IntToStr(L.Count-1)+',"violations":[';
  for I := 0 to P.DM_ViolationCount-1 do begin
   V := P.DM_Violations(I); if I>0 then Json := Json+',';
   Json := Json+'"'+StringReplace(V.DM_DetailString,'"','',MkSet(rfReplaceAll))+'"';
  end;
  R.Text := Json+']}'; R.SaveToFile(RESPONSE_FILE);
 finally R.Free; L.Free; end;
end;
