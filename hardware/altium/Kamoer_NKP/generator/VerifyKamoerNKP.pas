{ Reads back the symbol's footprint links from the generated SchLib and every
  pad/hole/slot and board-cutout count from the generated PcbLib, so the test
  can compare them against the catalogue numbers held in Test-KamoerNKP.ps1.

  IntegratedLibraryManager.ModelCount is reported too but NOT trusted: on
  2026-11-11 it kept returning the previous build's 2 links for a rebuilt
  IntLib whose LibCrossRef.Txt listed all 3 (it caches within an Altium
  session). The SchLib is the source the IntLib is compiled from. }

const
    REQUEST_FILE  = 'C:\Users\Public\treebeard_altium_nkp\verify_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_nkp\verify_response.json';
    SYMBOL_NAME   = 'Kamoer_NKP_12VDC';

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

{ One sorted "FP|pad|x|y|hole|P/N" entry per pad, "FP|cutout|xmin|ymin|xmax|ymax"
  per board cutout, plus "FP|cutouts|n" and "FP|regions|T n|B n|TM n|BM n|O n" (copper
  regions on top / bottom, top / bottom solder-mask regions, anything else). }
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
    Cutouts, I, NTop, NBot, NTopMask, NBotMask, NOther : Integer;
    S1T, S1B, S2T, S2B, SOther : Integer;
    PlatedStr : String;
    R : TCoordRect;
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
            Cutouts := 0; NTop := 0; NBot := 0; NTopMask := 0; NBotMask := 0; NOther := 0;
            S1T := 0; S1B := 0; S2T := 0; S2B := 0; SOther := 0;
            GIter := Comp.GroupIterator_Create;
            GIter.AddFilter_ObjectSet(MkSet(ePadObject, eRegionObject));
            Prim := GIter.FirstPCBObject;
            while Prim <> nil do
            begin
                if Prim.ObjectId = ePadObject then
                begin
                    Pad := Prim;
                    if Pad.Layer <> eMultiLayer then
                    begin
                        { SMD pad: counted per pin and layer, not listed. }
                        if (Pad.Name = '1') and (Pad.Layer = eTopLayer) then Inc(S1T)
                        else if (Pad.Name = '1') and (Pad.Layer = eBottomLayer) then Inc(S1B)
                        else if (Pad.Name = '2') and (Pad.Layer = eTopLayer) then Inc(S2T)
                        else if (Pad.Name = '2') and (Pad.Layer = eBottomLayer) then Inc(S2B)
                        else Inc(SOther);
                    end
                    else
                    begin
                    if Pad.Plated then PlatedStr := 'P' else PlatedStr := 'N';
                    if Pad.HoleType = eSlotHole then
                        PlatedStr := PlatedStr + '|S' + MmStr(Pad.HoleWidth) + '@' +
                                     FormatFloat('0', Pad.HoleRotation);
                    Lines.Add(Comp.Name + '|' + Pad.Name + '|' +
                              MmStr(Pad.X - Comp.X) + '|' + MmStr(Pad.Y - Comp.Y) + '|' +
                              MmStr(Pad.HoleSize) + '|' + PlatedStr);
                    end;
                end
                else if Prim.ObjectId = eRegionObject then
                begin
                    Region := Prim;
                    if Region.Kind = eRegionKind_BoardCutout then
                    begin
                        Inc(Cutouts);
                        R := Region.BoundingRectangle;
                        Lines.Add(Comp.Name + '|cutout|' +
                                  MmStr(R.Left - Comp.X) + '|' + MmStr(R.Bottom - Comp.Y) + '|' +
                                  MmStr(R.Right - Comp.X) + '|' + MmStr(R.Top - Comp.Y));
                    end
                    else if Region.Layer = eTopLayer then Inc(NTop)
                    else if Region.Layer = eBottomLayer then Inc(NBot)
                    else if Region.Layer = eTopSolder then Inc(NTopMask)
                    else if Region.Layer = eBottomSolder then Inc(NBotMask)
                    else Inc(NOther);
                end;
                Prim := GIter.NextPCBObject;
            end;
            Comp.GroupIterator_Destroy(GIter);
            Lines.Add(Comp.Name + '|cutouts|' + IntToStr(Cutouts));
            Lines.Add(Comp.Name + '|regions|T' + IntToStr(NTop) + '|B' + IntToStr(NBot) +
                      '|TM' + IntToStr(NTopMask) + '|BM' + IntToStr(NBotMask) + '|O' + IntToStr(NOther));
            Lines.Add(Comp.Name + '|smd|1T' + IntToStr(S1T) + '|1B' + IntToStr(S1B) +
                      '|2T' + IntToStr(S2T) + '|2B' + IntToStr(S2B) + '|O' + IntToStr(SOther));
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
