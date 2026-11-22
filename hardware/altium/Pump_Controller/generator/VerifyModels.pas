const REQUEST_FILE = '__REQUEST__'; RESPONSE_FILE = '__RESPONSE__';
function J(S : String) : String;
begin
 Result := StringReplace(StringReplace(S,'\','\\',MkSet(rfReplaceAll)),'"','\"',MkSet(rfReplaceAll));
end;
procedure Run;
var R,O : TStringList; I,N : Integer; Path,Symbol,Models,Json : String;
begin
 R := TStringList.Create; O := TStringList.Create;
 try
  R.LoadFromFile(REQUEST_FILE); Json := '[';
  I := 0;
  while I < R.Count-1 do begin
   Path := R[I]; Symbol := R[I+1];
   IntegratedLibraryManager.InstallLibrary(Path);
   N := IntegratedLibraryManager.ModelCount(Symbol,Path,'PCBLIB');
   Models := '';
   if N>0 then Models := IntegratedLibraryManager.ModelName(Symbol,Path,'PCBLIB',0);
   if I>0 then Json := Json+',';
   Json := Json+'{"symbol":"'+J(Symbol)+'","count":'+IntToStr(N)+',"first_model":"'+J(Models)+'"}';
   I := I+2;
  end;
  O.Text := Json+']'; O.SaveToFile(RESPONSE_FILE);
 finally R.Free; O.Free; end;
end;
