{ Reads back the symbol's footprint links from the generated SchLib and every
  pad/hole/slot and board-cutout count from the generated PcbLib, so the test
  can compare them against the catalogue numbers held in Test-BA4AAPC.ps1.

  IntegratedLibraryManager.ModelCount is reported too but NOT trusted: on
  2026-11-11 it kept returning the previous build's 2 links for a rebuilt
  IntLib whose LibCrossRef.Txt listed all 3 (it caches within an Altium
  session). The SchLib is the source the IntLib is compiled from. }

const
    REQUEST_FILE  = 'C:\Users\Public\treebeard_altium_ba4aapc\verify_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_ba4aapc\verify_response.json';
    SYMBOL_NAME   = 'BA4AAPC';

function JsonEscape(const S : String) : String;
var I : Integer;
begin
    Result := '';
    for I := 1 to Length(S) do
    begin
        if S[I] = '\' then Result := Result + '\\'
        else if S[I] = '"' then Result := Result + '\"'
        else Result := Result + S[I];
    end;
end;

function MmStr(const C : TCoord) : String;
begin
    Result := FormatFloat('0.00', C / 393700.7874015748);
end;

{ One sorted "FP|pad|x|y|hole|P/N" entry per pad, plus "FP|cutouts|n". }
function PadReport(const PcbLibPath : String) : String;
var
    Doc : IServerDocument;
    Lib : IPCB_Library;
    LibIter, Iter : IPCB_LibraryIterator;
    Comp : IPCB_LibComponent;
    GIter : IPCB_GroupIterator;
    Prim : IPCB_Primitive;
    Pad : IPCB_Pad;
    Region : IPCB_Region;
    Lines : TStringList;
    Cutouts, I : Integer;
    PlatedStr : String;
begin
    Result := '';
    Doc := Client.OpenDocument('PCBLIB', PcbLibPath);
    if Doc = nil then Exit;
    Client.ShowDocument(Doc);
    Lib := PCBServer.GetPCBLibraryByPath(PcbLibPath);
    if Lib = nil then Lib := PCBServer.GetCurrentPCBLibrary;
    if Lib = nil then Exit;
    Lines := TStringList.Create;
    try
        Lines.Sorted := True;
        LibIter := Lib.LibraryIterator_Create;
        LibIter.SetState_FilterAll;
        Comp := LibIter.FirstPCBObject;
        while Comp <> nil do
        begin
            Cutouts := 0;
            GIter := Comp.GroupIterator_Create;
            GIter.AddFilter_ObjectSet(MkSet(ePadObject, eRegionObject));
            Prim := GIter.FirstPCBObject;
            while Prim <> nil do
            begin
                if Prim.ObjectId = ePadObject then
                begin
                    Pad := Prim;
                    if Pad.Plated then PlatedStr := 'P' else PlatedStr := 'N';
                    if Pad.HoleType = eSlotHole then
                        PlatedStr := PlatedStr + '|S' + MmStr(Pad.HoleWidth) + '@' +
                                     FormatFloat('0', Pad.HoleRotation);
                    Lines.Add(Comp.Name + '|' + Pad.Name + '|' +
                              MmStr(Pad.X - Comp.X) + '|' + MmStr(Pad.Y - Comp.Y) + '|' +
                              MmStr(Pad.HoleSize) + '|' + PlatedStr);
                end
                else if Prim.ObjectId = eRegionObject then
                begin
                    Region := Prim;
                    if Region.Kind = eRegionKind_BoardCutout then Inc(Cutouts);
                end;
                Prim := GIter.NextPCBObject;
            end;
            Comp.GroupIterator_Destroy(GIter);
            Lines.Add(Comp.Name + '|cutouts|' + IntToStr(Cutouts));
            Comp := LibIter.NextPCBObject;
        end;
        Lib.LibraryIterator_Destroy(LibIter);
        for I := 0 to Lines.Count - 1 do
        begin
            if I > 0 then Result := Result + ';';
            Result := Result + Lines[I];
        end;
    finally
        Lines.Free;
    end;
    Client.CloseDocument(Doc);
end;

{ "name*" for the current model, comma-separated, PCBLIB models only. }
function ImplReport(const SchLibPath : String) : String;
var
    Doc : IServerDocument;
    Lib : ISch_Lib;
    LibIter, ImplIter : ISch_Iterator;
    Comp : ISch_Component;
    Impl : ISch_Implementation;
    Lines : TStringList;
    I : Integer;
    S : String;
begin
    Result := '';
    Doc := Client.OpenDocument('SCHLIB', SchLibPath);
    if Doc = nil then Exit;
    Client.ShowDocument(Doc);
    Lib := SchServer.GetSchDocumentByPath(SchLibPath);
    if Lib = nil then Exit;
    Lines := TStringList.Create;
    try
        Lines.Sorted := True;
        LibIter := Lib.SchLibIterator_Create;
        LibIter.AddFilter_ObjectSet(MkSet(eSchComponent));
        Comp := LibIter.FirstSchObject;
        while Comp <> nil do
        begin
            if Comp.LibReference = SYMBOL_NAME then
            begin
                ImplIter := Comp.SchIterator_Create;
                ImplIter.AddFilter_ObjectSet(MkSet(eImplementation));
                Impl := ImplIter.FirstSchObject;
                while Impl <> nil do
                begin
                    if UpperCase(Impl.ModelType) = 'PCBLIB' then
                    begin
                        S := Impl.ModelName;
                        if Impl.IsCurrent then S := S + '*';
                        Lines.Add(S);
                    end;
                    Impl := ImplIter.NextSchObject;
                end;
                Comp.SchIterator_Destroy(ImplIter);
            end;
            Comp := LibIter.NextSchObject;
        end;
        Lib.SchIterator_Destroy(LibIter);
        for I := 0 to Lines.Count - 1 do
        begin
            if I > 0 then Result := Result + ',';
            Result := Result + Lines[I];
        end;
    finally
        Lines.Free;
    end;
    Client.CloseDocument(Doc);
end;

procedure Run;
var
    Request, Response : TStringList;
    LibraryPath, PcbLibPath, SchLibPath, Names : String;
    ModelCount, I : Integer;
begin
    Request := TStringList.Create;
    Response := TStringList.Create;
    try
        if not FileExists(REQUEST_FILE) then
        begin
            Response.Text := '{"error":"missing request"}';
            Response.SaveToFile(RESPONSE_FILE);
            Exit;
        end;
        Request.LoadFromFile(REQUEST_FILE);
        if Request.Count < 3 then
        begin
            Response.Text := '{"error":"request needs IntLib, PcbLib and SchLib paths"}';
            Response.SaveToFile(RESPONSE_FILE);
            Exit;
        end;
        LibraryPath := Trim(Request[0]);
        PcbLibPath  := Trim(Request[1]);
        SchLibPath  := Trim(Request[2]);
        ModelCount := IntegratedLibraryManager.ModelCount(
            SYMBOL_NAME, LibraryPath, 'PCBLIB');
        Names := '';
        for I := 0 to ModelCount - 1 do
        begin
            if I > 0 then Names := Names + ',';
            Names := Names + '"' + JsonEscape(IntegratedLibraryManager.ModelName(
                SYMBOL_NAME, LibraryPath, 'PCBLIB', I)) + '"';
        end;
        Response.Text := '{"model_count":' + IntToStr(ModelCount) +
                         ',"model_names":[' + Names + ']' +
                         ',"sch_impls":"' + JsonEscape(ImplReport(SchLibPath)) + '"' +
                         ',"pad_report":"' + JsonEscape(PadReport(PcbLibPath)) + '"}';
        Response.SaveToFile(RESPONSE_FILE);
    finally
        Request.Free;
        Response.Free;
    end;
end;
