const REQUEST_FILE='__REQUEST__'; RESPONSE_FILE='__RESPONSE__';
procedure Run;
var R,O : TStringList; P : IProject; W : IWorkspace; I : Integer; Json : String;
begin
 R := TStringList.Create; O := TStringList.Create;
 try
  R.LoadFromFile(REQUEST_FILE); W := GetWorkspace; Json := '[';
  for I := 0 to R.Count-1 do begin
   P := W.DM_OpenProject(R[I],False); P.DM_Compile;
   IntegratedLibraryManager.CreateIntegratedLibrary(P,ChangeFileExt(R[I],'.IntLib'),True);
   if I>0 then Json := Json+',';
   Json := Json+'"'+ExtractFileName(R[I])+'"';
  end;
  O.Text := Json+']'; O.SaveToFile(RESPONSE_FILE);
 finally R.Free; O.Free; end;
end;
