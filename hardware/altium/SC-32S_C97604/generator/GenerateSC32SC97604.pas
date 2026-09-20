{ Generates a ready-to-install Altium integrated library for Seiko C97604. }

const
    BRIDGE_DIR   = 'C:\Users\Public\treebeard_altium_sc32s\';
    REQUEST_FILE = 'C:\Users\Public\treebeard_altium_sc32s\generate_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_sc32s\generate_response.json';
    LIB_BASENAME = 'SC-32S_C97604';
    SYMBOL_NAME  = 'SC-32S32.768kHz20PPM7pF';
    FOOTPRINT_NAME = 'SC-32S_SMD3215_2P';

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

procedure WriteResponse(const Status, Detail : String);
var Lines : TStringList;
begin
    if not DirectoryExists(BRIDGE_DIR) then ForceDirectories(BRIDGE_DIR);
    Lines := TStringList.Create;
    try
        Lines.Text := '{"status":"' + JsonEscape(Status) + '","detail":"' +
                      JsonEscape(Detail) + '"}';
        Lines.SaveToFile(RESPONSE_FILE);
    finally
        Lines.Free;
    end;
end;

function ReadOutputDir : String;
var Lines : TStringList;
begin
    Result := '';
    if not FileExists(REQUEST_FILE) then Exit;
    Lines := TStringList.Create;
    try
        Lines.LoadFromFile(REQUEST_FILE);
        if Lines.Count > 0 then Result := Trim(Lines[0]);
    finally
        Lines.Free;
    end;
    if (Result <> '') and (Result[Length(Result)] <> '\') then Result := Result + '\';
end;

function Mm(const Value : Double) : TCoord;
begin
    Result := Round(Value * 393700.7874015748);
end;

procedure RegisterPcbPrimitive(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                               Prim : IPCB_Primitive);
begin
    Comp.AddPCBObject(Prim);
    Lib.Board.AddPCBObject(Prim);
    PCBServer.SendMessageToRobots(Comp.I_ObjectAddress, c_Broadcast,
                                  PCBM_BoardRegisteration, Prim.I_ObjectAddress);
    PCBServer.SendMessageToRobots(Lib.Board.I_ObjectAddress, c_Broadcast,
                                  PCBM_BoardRegisteration, Prim.I_ObjectAddress);
end;

procedure AddSmdPad(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                    Xmm, Ymm, Wmm, Hmm : Double; Name : String);
var Pad : IPCB_Pad;
begin
    Pad := PCBServer.PCBObjectFactory(ePadObject, eNoDimension, eCreate_Default);
    if Pad = nil then Exit;
    Pad.Name     := Name;
    Pad.X        := Comp.X + Mm(Xmm);
    Pad.Y        := Comp.Y + Mm(Ymm);
    Pad.Mode     := ePadMode_Simple;
    Pad.Layer    := eTopLayer;
    Pad.HoleSize := 0;
    Pad.TopXSize := Mm(Wmm);
    Pad.TopYSize := Mm(Hmm);
    Pad.SetState_StackShapeOnLayer(Pad.Layer, eRoundedRectangular);
    Pad.SetState_StackCRPctOnLayer(Pad.Layer, 20);
    RegisterPcbPrimitive(Lib, Comp, Pad);
end;

procedure AddPcbTrack(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                      X1mm, Y1mm, X2mm, Y2mm, Wmm : Double; Layer : TLayer);
var Track : IPCB_Track;
begin
    Track := PCBServer.PCBObjectFactory(eTrackObject, eNoDimension, eCreate_Default);
    if Track = nil then Exit;
    Track.X1    := Comp.X + Mm(X1mm);
    Track.Y1    := Comp.Y + Mm(Y1mm);
    Track.X2    := Comp.X + Mm(X2mm);
    Track.Y2    := Comp.Y + Mm(Y2mm);
    Track.Width := Mm(Wmm);
    Track.Layer := Layer;
    RegisterPcbPrimitive(Lib, Comp, Track);
end;

procedure AddPcbBox(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                    X1mm, Y1mm, X2mm, Y2mm, Wmm : Double; Layer : TLayer);
begin
    AddPcbTrack(Lib, Comp, X1mm, Y1mm, X2mm, Y1mm, Wmm, Layer);
    AddPcbTrack(Lib, Comp, X2mm, Y1mm, X2mm, Y2mm, Wmm, Layer);
    AddPcbTrack(Lib, Comp, X2mm, Y2mm, X1mm, Y2mm, Wmm, Layer);
    AddPcbTrack(Lib, Comp, X1mm, Y2mm, X1mm, Y1mm, Wmm, Layer);
end;

procedure AddExtrudedBody(Lib : IPCB_Library; Comp : IPCB_LibComponent);
var Body : IPCB_ComponentBody; Contour : IPCB_Contour;
begin
    Body := PCBServer.PCBObjectFactory(eComponentBodyObject, eNoDimension, eCreate_Default);
    if Body = nil then Exit;
    Body.BodyProjection := eBoardSide_Top;
    Body.Layer          := LayerUtils.MechanicalLayer(13);
    Body.StandoffHeight := 0;
    Body.OverallHeight  := Mm(0.85);
    Body.BodyColor3D    := $C8C8C8;
    Body.BodyOpacity3D  := 1.0;
    Contour := Body.MainContour.Replicate;
    Contour.Count := 4;
    Contour.X[1] := Comp.X + Mm(-1.60); Contour.Y[1] := Comp.Y + Mm(-0.75);
    Contour.X[2] := Comp.X + Mm( 1.60); Contour.Y[2] := Comp.Y + Mm(-0.75);
    Contour.X[3] := Comp.X + Mm( 1.60); Contour.Y[3] := Comp.Y + Mm( 0.75);
    Contour.X[4] := Comp.X + Mm(-1.60); Contour.Y[4] := Comp.Y + Mm( 0.75);
    Body.SetOutlineContour(Contour);
    RegisterPcbPrimitive(Lib, Comp, Body);
end;

procedure GeneratePcbLib(const OutputDir : String);
var
    Doc : IServerDocument;
    Lib : IPCB_Library;
    Comp, DefaultComp : IPCB_LibComponent;
begin
    Doc := CreateNewDocumentFromDocumentKind('PCBLIB');
    if Doc = nil then Exit;
    Lib := PCBServer.GetCurrentPCBLibrary;
    if Lib = nil then Exit;
    DefaultComp := Lib.CurrentComponent;
    Comp := PCBServer.CreatePCBLibComp;
    if Comp = nil then Exit;
    Comp.Name := FOOTPRINT_NAME;
    Comp.Description := 'Seiko SC-32S 3215 two-pad crystal; manufacturer recommended land pattern';
    Lib.RegisterComponent(Comp);
    Lib.CurrentComponent := Comp;
    PCBServer.PreProcess;
    try
        { Seiko pattern: two 1.0 x 1.8 mm pads, 1.0 mm inner gap. }
        AddSmdPad(Lib, Comp, -1.00, 0, 1.00, 1.80, '1');
        AddSmdPad(Lib, Comp,  1.00, 0, 1.00, 1.80, '2');
        { Silk exists only in the pad-free center gap. }
        AddPcbTrack(Lib, Comp, -0.38, -0.68, 0.38, -0.68, 0.12, eTopOverlay);
        AddPcbTrack(Lib, Comp, -0.38,  0.68, 0.38,  0.68, 0.12, eTopOverlay);
        AddPcbBox(Lib, Comp, -1.85, -1.15, 1.85, 1.15, 0.05, eMechanical1);
        AddPcbBox(Lib, Comp, -1.60, -0.75, 1.60, 0.75, 0.10, eMechanical13);
        AddExtrudedBody(Lib, Comp);
    finally
        PCBServer.PostProcess;
    end;
    if DefaultComp <> nil then
    begin
        Lib.DeRegisterComponent(DefaultComp);
        Lib.RemoveComponent(DefaultComp);
    end;
    Lib.Board.ViewManager_FullUpdate;
    Doc.SetModified(True);
    Doc.DoSafeChangeFileNameAndSave(OutputDir + LIB_BASENAME + '.PcbLib', 'PCBLIB');
end;

procedure RegisterSchPrimitive(Comp : ISch_Component; Prim : ISch_GraphicalObject);
begin
    Comp.AddSchObject(Prim);
    SchServer.RobotManager.SendMessage(Comp.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Prim.I_ObjectAddress);
end;

procedure AddPin(Comp : ISch_Component; X, Y, Len : Integer;
                 Orientation : TRotationBy90; Designator : String);
var Pin : ISch_Pin;
begin
    Pin := SchServer.SchObjectFactory(ePin, eCreate_Default);
    if Pin = nil then Exit;
    Pin.Location             := Point(MilsToCoord(X), MilsToCoord(Y));
    Pin.Orientation          := Orientation;
    Pin.PinLength            := MilsToCoord(Len);
    Pin.Electrical           := eElectricPassive;
    Pin.Designator           := Designator;
    Pin.Name                 := '';
    Pin.ShowDesignator       := False;
    Pin.ShowName             := False;
    Pin.OwnerPartId          := 1;
    Pin.OwnerPartDisplayMode := Comp.DisplayMode;
    RegisterSchPrimitive(Comp, Pin);
end;

procedure AddSchLine(Comp : ISch_Component; X1, Y1, X2, Y2 : Integer);
var Line : ISch_Line;
begin
    Line := SchServer.SchObjectFactory(eLine, eCreate_Default);
    if Line = nil then Exit;
    Line.Location             := Point(MilsToCoord(X1), MilsToCoord(Y1));
    Line.Corner               := Point(MilsToCoord(X2), MilsToCoord(Y2));
    Line.LineWidth            := eSmall;
    Line.LineStyle            := eLineStyleSolid;
    Line.Color                := $000000;
    Line.OwnerPartId          := 1;
    Line.OwnerPartDisplayMode := Comp.DisplayMode;
    RegisterSchPrimitive(Comp, Line);
end;

procedure AddSchRect(Comp : ISch_Component; X1, Y1, X2, Y2 : Integer);
var Rect : ISch_Rectangle;
begin
    Rect := SchServer.SchObjectFactory(eRectangle, eCreate_Default);
    if Rect = nil then Exit;
    Rect.Location             := Point(MilsToCoord(X1), MilsToCoord(Y1));
    Rect.Corner               := Point(MilsToCoord(X2), MilsToCoord(Y2));
    Rect.LineWidth            := eSmall;
    Rect.Color                := $000000;
    Rect.AreaColor            := $FFFFFF;
    Rect.IsSolid              := False;
    Rect.Transparent          := True;
    Rect.OwnerPartId          := 1;
    Rect.OwnerPartDisplayMode := Comp.DisplayMode;
    RegisterSchPrimitive(Comp, Rect);
end;

procedure AddParameter(Comp : ISch_Component; Name, Value : String);
var Param : ISch_Parameter;
begin
    Param := SchServer.SchObjectFactory(eParameter, eCreate_Default);
    if Param = nil then Exit;
    Param.Name                 := Name;
    Param.Text                 := Value;
    Param.IsHidden             := True;
    Param.Location             := Point(0, 0);
    Param.Orientation          := eRotate0;
    Param.FontID               := 1;
    Param.OwnerPartId          := 1;
    Param.OwnerPartDisplayMode := Comp.DisplayMode;
    RegisterSchPrimitive(Comp, Param);
end;

procedure AddFootprint(Comp : ISch_Component);
var Impl : ISch_Implementation;
begin
    Impl := Comp.AddSchImplementation;
    if Impl = nil then Exit;
    Impl.ModelName   := FOOTPRINT_NAME;
    Impl.ModelType   := 'PCBLIB';
    Impl.Description := 'Seiko SC-32S manufacturer recommended 3215 two-pad footprint';
    Impl.IsCurrent   := True;
    Impl.AddDataFileLink(FOOTPRINT_NAME, '', 'PCBLib');
end;

procedure GenerateSchLib(const OutputDir : String);
var
    Doc : IServerDocument;
    Lib : ISch_Lib;
    Comp, DefaultComp : ISch_Component;
begin
    Doc := CreateNewDocumentFromDocumentKind('SCHLIB');
    if Doc = nil then Exit;
    Lib := SchServer.GetCurrentSchDocument;
    if Lib = nil then Exit;
    DefaultComp := Lib.CurrentSchComponent;
    Comp := SchServer.SchObjectFactory(eSchComponent, eCreate_Default);
    if Comp = nil then Exit;
    Comp.LibReference         := SYMBOL_NAME;
    Comp.Designator.Text      := 'Y?';
    Comp.ComponentDescription := '32.768 kHz 7 pF +/-20 ppm SMD3215-2P crystal resonator';
    Comp.PartCount            := 1;
    Comp.CurrentPartId        := 1;
    Comp.DisplayMode          := 0;
    Lib.AddSchComponent(Comp);
    Comp.LibReference := SYMBOL_NAME;
    Comp.Designator.IsHidden := False;
    Comp.Comment.Text := '32.768kHz 7pF';
    Comp.Comment.IsHidden := False;
    SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Comp.I_ObjectAddress);
    Lib.CurrentSchComponent := Comp;

    AddPin(Comp, 100, 0, 100, eRotate180, '1');
    AddPin(Comp, 300, 0, 100, eRotate0, '2');
    AddSchLine(Comp, 100, 0, 140, 0);
    AddSchLine(Comp, 140, -80, 140, 80);
    AddSchRect(Comp, 170, -95, 230, 95);
    AddSchLine(Comp, 260, -80, 260, 80);
    AddSchLine(Comp, 260, 0, 300, 0);

    AddParameter(Comp, 'Component Type', 'Crystal Resonator');
    AddParameter(Comp, 'Frequency', '32.768kHz');
    AddParameter(Comp, 'Load Capacitance', '7pF');
    AddParameter(Comp, 'Frequency Tolerance', '+/-20ppm');
    AddParameter(Comp, 'ESR', '70kOhm max');
    AddParameter(Comp, 'Operating Temperature', '-40C to +85C');
    AddParameter(Comp, 'Package', 'SMD3215-2P');
    AddParameter(Comp, 'Manufacturer', 'Seiko Instruments Inc.');
    AddParameter(Comp, 'Manufacturer Part Number', SYMBOL_NAME);
    AddParameter(Comp, 'LCSC Part #', 'C97604');
    AddParameter(Comp, 'Supplier', 'LCSC');
    AddParameter(Comp, 'Supplier Part Number', 'C97604');
    AddParameter(Comp, 'Do Not Substitute', 'Yes');
    AddFootprint(Comp);

    if DefaultComp <> nil then Lib.RemoveSchComponent(DefaultComp);
    Lib.GraphicallyInvalidate;
    Doc.SetModified(True);
    Doc.DoSafeChangeFileNameAndSave(OutputDir + LIB_BASENAME + '.SchLib', 'SCHLIB');
end;

procedure GenerateLibPkg(const OutputDir : String);
var Lines : TStringList;
begin
    Lines := TStringList.Create;
    try
        Lines.Add('[Design]');
        Lines.Add('Version=1.0');
        Lines.Add('HierarchyMode=0');
        Lines.Add('OpenOutputs=0');
        Lines.Add('OutputPath=');
        Lines.Add('UserID=0xFFFFFFFF');
        Lines.Add('ReorderDocumentsOnCompile=1');
        Lines.Add('');
        Lines.Add('[Document1]');
        Lines.Add('DocumentPath=' + LIB_BASENAME + '.PcbLib');
        Lines.Add('AnnotationEnabled=1');
        Lines.Add('DoLibraryUpdate=1');
        Lines.Add('DoDatabaseUpdate=1');
        Lines.Add('');
        Lines.Add('[Document2]');
        Lines.Add('DocumentPath=' + LIB_BASENAME + '.SchLib');
        Lines.Add('AnnotationEnabled=1');
        Lines.Add('DoLibraryUpdate=1');
        Lines.Add('DoDatabaseUpdate=1');
        Lines.SaveToFile(OutputDir + LIB_BASENAME + '.LibPkg');
    finally
        Lines.Free;
    end;
end;

procedure CompileIntLib(const OutputDir : String);
var
    Workspace : IWorkspace;
    Project : IProject;
begin
    Workspace := GetWorkspace;
    if Workspace = nil then Exit;
    Project := Workspace.DM_OpenProject(OutputDir + LIB_BASENAME + '.LibPkg', False);
    if Project = nil then Exit;
    Project.DM_Compile;
    IntegratedLibraryManager.CreateIntegratedLibrary(
        Project, OutputDir + LIB_BASENAME + '.IntLib', True);
end;

procedure Run;
var OutputDir : String;
begin
    OutputDir := ReadOutputDir;
    if OutputDir = '' then
    begin
        WriteResponse('error', 'missing output directory in generate_request.txt');
        Exit;
    end;
    if not DirectoryExists(OutputDir) then ForceDirectories(OutputDir);
    try
        GeneratePcbLib(OutputDir);
        GenerateSchLib(OutputDir);
        GenerateLibPkg(OutputDir);
        CompileIntLib(OutputDir);
        if FileExists(OutputDir + LIB_BASENAME + '.IntLib') then
            WriteResponse('ok', 'generated compiled SC-32S_C97604.IntLib')
        else
            WriteResponse('error', 'source libraries generated but IntLib compilation failed');
    except
        WriteResponse('error', 'exception during SC-32S library generation');
    end;
end;
