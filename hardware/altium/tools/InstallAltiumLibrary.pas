{ Installs one integrated library into the running Altium so its parts appear in the
  Components panel, replacing any installed copy with the same file name, then closes every
  leftover generator/verifier/installer script project under %TEMP%\treebeard-*.

  Request file, one line: the absolute .IntLib path.
  Response file: what was uninstalled/installed, the installed list, and DONE. }

const
    BRIDGE   = 'C:\Users\Public\treebeard_altium_install\';
    REQUEST  = 'C:\Users\Public\treebeard_altium_install\request.txt';
    RESPONSE = 'C:\Users\Public\treebeard_altium_install\response.txt';

function FileNameOf(const P : String) : String;
var I : Integer;
begin
    Result := P;
    for I := Length(P) downto 1 do
        if P[I] = '\' then
        begin
            Result := Copy(P, I + 1, Length(P) - I);
            Exit;
        end;
end;

procedure CloseTempScriptProjects(Log : TStringList);
var W : IWorkspace; I, K : Integer; Paths, Docs : TStringList; P : String;
    Prj : IProject; D : IServerDocument;
begin
    Paths := TStringList.Create;
    try
        W := GetWorkspace;
        for I := 0 to W.DM_ProjectCount - 1 do
        begin
            P := W.DM_Projects(I).DM_ProjectFullPath;
            if Pos('\APPDATA\LOCAL\TEMP\TREEBEARD-', UpperCase(P)) > 0 then Paths.Add(P);
        end;
        for I := 0 to Paths.Count - 1 do
        begin
            for K := 0 to W.DM_ProjectCount - 1 do
            begin
                Prj := W.DM_Projects(K);
                if Prj.DM_ProjectFullPath = Paths[I] then Break;
                Prj := nil;
            end;
            if Prj <> nil then
            begin
                Docs := TStringList.Create;
                try
                    for K := 0 to Prj.DM_LogicalDocumentCount - 1 do
                        Docs.Add(Prj.DM_LogicalDocuments(K).DM_FullPath);
                    for K := 0 to Docs.Count - 1 do
                    begin
                        D := Client.GetDocumentByPath(Docs[K]);
                        if D <> nil then begin D.SetModified(False); Client.CloseDocument(D); end;
                    end;
                finally
                    Docs.Free;
                end;
            end;
            D := Client.GetDocumentByPath(Paths[I]);
            if D <> nil then begin D.SetModified(False); Client.CloseDocument(D); end;
            Log.Add('CLOSED ' + Paths[I]);
        end;
    finally
        Paths.Free;
    end;
end;

procedure Run;
var Req, Log, Del : TStringList; Lib, Name, P : String; I : Integer;
begin
    Req := TStringList.Create; Log := TStringList.Create; Del := TStringList.Create;
    try
        if not DirectoryExists(BRIDGE) then ForceDirectories(BRIDGE);
        if FileExists(REQUEST) then Req.LoadFromFile(REQUEST);
        if (Req.Count = 0) or not FileExists(Trim(Req[0])) then
        begin
            Log.Add('ERROR missing or nonexistent IntLib path in request');
        end
        else
        begin
            Lib := Trim(Req[0]);
            Name := UpperCase(FileNameOf(Lib));
            for I := 0 to IntegratedLibraryManager.InstalledLibraryCount - 1 do
            begin
                P := IntegratedLibraryManager.InstalledLibraryPath(I);
                if UpperCase(FileNameOf(P)) = Name then Del.Add(P);
            end;
            for I := 0 to Del.Count - 1 do
            begin
                IntegratedLibraryManager.UninstallLibrary(Del[I]);
                Log.Add('UNINSTALLED ' + Del[I]);
            end;
            IntegratedLibraryManager.InstallLibrary(Lib);
            Log.Add('INSTALLED ' + Lib);
        end;
        CloseTempScriptProjects(Log);
        Log.Add('--- NOW INSTALLED');
        for I := 0 to IntegratedLibraryManager.InstalledLibraryCount - 1 do
            Log.Add(IntegratedLibraryManager.InstalledLibraryPath(I));
        Log.Add('DONE');
    finally
        Log.SaveToFile(RESPONSE);
        Req.Free; Log.Free; Del.Free;
    end;
end;
