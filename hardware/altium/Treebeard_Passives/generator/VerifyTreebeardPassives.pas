{ Reopens the generated libraries in Altium and reports what Altium resolves. }

const
    BRIDGE_DIR = 'C:\Users\Public\treebeard_altium_passives\';
    REQUEST_FILE = 'C:\Users\Public\treebeard_altium_passives\verify_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_passives\verify_response.json';

function JsonEscape(const S : String) : String;
var I, C : Integer;
begin
    Result := '';
    for I := 1 to Length(S) do
    begin
        C := Ord(S[I]);
        if (C > 126) or (C < 32) then Result := Result + '\u' + IntToHex(C, 4)
        else if S[I] = '\' then Result := Result + '\\'
        else if S[I] = '"' then Result := Result + '\"'
        else Result := Result + S[I];
    end;
end;

function CountPcbKind(Comp : IPCB_LibComponent; ObjectId : Integer) : Integer;
var
    Iter : IPCB_GroupIterator;
    Prim : IPCB_Primitive;
begin
    Result := 0;
    Iter := Comp.GroupIterator_Create;
    Iter.AddFilter_ObjectSet(MkSet(ObjectId));
    Prim := Iter.FirstPCBObject;
    while Prim <> nil do
    begin
        Inc(Result);
        Prim := Iter.NextPCBObject;
    end;
    Comp.GroupIterator_Destroy(Iter);
end;

function CountSchKind(Comp : ISch_Component; ObjectId : Integer) : Integer;
var
    Iter : ISch_Iterator;
    Prim : ISch_GraphicalObject;
begin
    Result := 0;
    Iter := Comp.SchIterator_Create;
    Iter.AddFilter_ObjectSet(MkSet(ObjectId));
    Prim := Iter.FirstSchObject;
    while Prim <> nil do
    begin
        Inc(Result);
        Prim := Iter.NextSchObject;
    end;
    Comp.SchIterator_Destroy(Iter);
end;

function PcbDetails : String;
var
    Lib : IPCB_Library;
    Iter : IPCB_LibraryIterator;
    Comp : IPCB_LibComponent;
    First : Boolean;
begin
    Result := '[';
    First := True;
    Lib := PCBServer.GetCurrentPCBLibrary;
    if Lib <> nil then
    begin
        Iter := Lib.LibraryIterator_Create;
        Iter.SetState_FilterAll;
        Comp := Iter.FirstPCBObject;
        while Comp <> nil do
        begin
            if not First then Result := Result + ',';
            First := False;
            Result := Result + '{"name":"' + JsonEscape(Comp.Name) + '"' +
                ',"pads":' + IntToStr(CountPcbKind(Comp, ePadObject)) +
                ',"tracks":' + IntToStr(CountPcbKind(Comp, eTrackObject)) +
                ',"bodies":' + IntToStr(CountPcbKind(Comp, eComponentBodyObject)) + '}';
            Comp := Iter.NextPCBObject;
        end;
        Lib.LibraryIterator_Destroy(Iter);
    end;
    Result := Result + ']';
end;

function SchDetails : String;
var
    Lib : ISch_Lib;
    Iter : ISch_Iterator;
    Comp : ISch_Component;
    First : Boolean;
begin
    Result := '[';
    First := True;
    Lib := SchServer.GetCurrentSchDocument;
    if Lib <> nil then
    begin
        Iter := Lib.SchLibIterator_Create;
        Iter.AddFilter_ObjectSet(MkSet(eSchComponent));
        Comp := Iter.FirstSchObject;
        while Comp <> nil do
        begin
            if not First then Result := Result + ',';
            First := False;
            Result := Result + '{"name":"' + JsonEscape(Comp.LibReference) + '"' +
                ',"comment":"' + JsonEscape(Comp.Comment.Text) + '"' +
                ',"pins":' + IntToStr(CountSchKind(Comp, ePin)) +
                ',"lines":' + IntToStr(CountSchKind(Comp, eLine)) +
                ',"rectangles":' + IntToStr(CountSchKind(Comp, eRectangle)) +
                ',"parameters":' + IntToStr(CountSchKind(Comp, eParameter)) +
                ',"implementations":' + IntToStr(CountSchKind(Comp, eImplementation)) + '}';
            Comp := Iter.NextSchObject;
        end;
        Lib.SchIterator_Destroy(Iter);
    end;
    Result := Result + ']';
end;

procedure Run;
var
    Requests, Response : TStringList;
    I, Emitted : Integer;
    Path, Ext, Kind, Details, Json : String;
    Doc : IServerDocument;
    Opened : Boolean;
begin
    if not DirectoryExists(BRIDGE_DIR) then ForceDirectories(BRIDGE_DIR);
    Requests := TStringList.Create;
    Response := TStringList.Create;
    try
        if not FileExists(REQUEST_FILE) then
        begin
            Response.Text := '{"error":"missing verify_request.txt"}';
            Response.SaveToFile(RESPONSE_FILE);
            Exit;
        end;
        Requests.LoadFromFile(REQUEST_FILE);
        Json := '[';
        Emitted := 0;
        for I := 0 to Requests.Count - 1 do
        begin
            Path := Trim(Requests[I]);
            if Path = '' then Continue;
            Ext := UpperCase(ExtractFileExt(Path));
            if Ext = '.PCBLIB' then Kind := 'PCBLIB'
            else if Ext = '.SCHLIB' then Kind := 'SCHLIB'
            else Kind := '';
            Opened := False;
            Details := '[]';
            if (Kind <> '') and FileExists(Path) then
            begin
                Doc := Client.OpenDocument(Kind, Path);
                if Doc <> nil then
                begin
                    Client.ShowDocument(Doc);
                    Opened := True;
                    if Kind = 'PCBLIB' then Details := PcbDetails
                    else Details := SchDetails;
                end;
            end;
            if Emitted > 0 then Json := Json + ',';
            Json := Json + '{"file":"' + JsonEscape(Path) + '","kind":"' + Kind + '","opened":';
            if Opened then Json := Json + 'true' else Json := Json + 'false';
            Json := Json + ',"components":' + Details + '}';
            Inc(Emitted);
        end;
        Json := Json + ']';
        Response.Text := Json;
        Response.SaveToFile(RESPONSE_FILE);
    finally
        Requests.Free;
        Response.Free;
    end;
end;
