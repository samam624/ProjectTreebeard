{ Treebeard passive-library generator.

  This script is intentionally limited to commodity 0805 resistors and MLCCs.
  It creates genuine Altium .PcbLib/.SchLib files using the installed editor.
  Build-TreebeardPassives.ps1 supplies the output directory in REQUEST_FILE.
}

const
    BRIDGE_DIR  = 'C:\Users\Public\treebeard_altium_passives\';
    REQUEST_FILE = 'C:\Users\Public\treebeard_altium_passives\generate_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_passives\generate_response.json';

function JsonEscape(const S : String) : String;
var
    I : Integer;
begin
    Result := '';
    for I := 1 to Length(S) do
    begin
        if S[I] = '\' then Result := Result + '\\'
        else if S[I] = '"' then Result := Result + '\"'
        else Result := Result + S[I];
    end;
end;

procedure WriteResponse(const Status : String; const Detail : String);
var
    Lines : TStringList;
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
var
    Lines : TStringList;
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
    { Altium internal coordinates are 10,000 units per mil. }
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
var
    Pad : IPCB_Pad;
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
    Pad.SetState_StackCRPctOnLayer(Pad.Layer, 25);
    RegisterPcbPrimitive(Lib, Comp, Pad);
end;

procedure AddPcbTrack(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                      X1mm, Y1mm, X2mm, Y2mm, Wmm : Double; Layer : TLayer);
var
    Track : IPCB_Track;
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

procedure AddExtrudedBody(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                          Wmm, Hmm, HeightMm : Double; Color : Integer);
var
    Body : IPCB_ComponentBody;
    Contour : IPCB_Contour;
begin
    Body := PCBServer.PCBObjectFactory(eComponentBodyObject, eNoDimension, eCreate_Default);
    if Body = nil then Exit;
    Body.BodyProjection := eBoardSide_Top;
    Body.Layer          := LayerUtils.MechanicalLayer(13);
    Body.StandoffHeight := 0;
    Body.OverallHeight  := Mm(HeightMm);
    Body.BodyColor3D    := Color;
    Body.BodyOpacity3D  := 1.0;
    Contour := Body.MainContour.Replicate;
    Contour.Count := 4;
    Contour.X[1] := Comp.X + Mm(-Wmm / 2); Contour.Y[1] := Comp.Y + Mm(-Hmm / 2);
    Contour.X[2] := Comp.X + Mm( Wmm / 2); Contour.Y[2] := Comp.Y + Mm(-Hmm / 2);
    Contour.X[3] := Comp.X + Mm( Wmm / 2); Contour.Y[3] := Comp.Y + Mm( Hmm / 2);
    Contour.X[4] := Comp.X + Mm(-Wmm / 2); Contour.Y[4] := Comp.Y + Mm( Hmm / 2);
    Body.SetOutlineContour(Contour);
    RegisterPcbPrimitive(Lib, Comp, Body);
end;

function NewFootprint(Lib : IPCB_Library; Name, Description : String) : IPCB_LibComponent;
var
    Comp : IPCB_LibComponent;
begin
    Result := nil;
    Comp := PCBServer.CreatePCBLibComp;
    if Comp = nil then Exit;
    Comp.Name := Name;
    Comp.Description := Description;
    Lib.RegisterComponent(Comp);
    Lib.CurrentComponent := Comp;
    Result := Comp;
end;

procedure BuildResistorFootprint(Lib : IPCB_Library);
var
    Comp : IPCB_LibComponent;
begin
    Comp := NewFootprint(Lib, 'TB_R_0805_2012M',
        '0805 resistor, IPC-7351 nominal, 2.0 x 1.25 mm body; M1 courtyard, M13 assembly');
    if Comp = nil then Exit;
    PCBServer.PreProcess;
    try
        AddSmdPad(Lib, Comp, -0.9125, 0, 1.025, 1.400, '1');
        AddSmdPad(Lib, Comp,  0.9125, 0, 1.025, 1.400, '2');
        AddPcbTrack(Lib, Comp, -0.227064, -0.735, 0.227064, -0.735, 0.120, eTopOverlay);
        AddPcbTrack(Lib, Comp, -0.227064,  0.735, 0.227064,  0.735, 0.120, eTopOverlay);
        AddPcbBox(Lib, Comp, -1.680, -0.950, 1.680, 0.950, 0.050, eMechanical1);
        AddPcbBox(Lib, Comp, -1.000, -0.625, 1.000, 0.625, 0.100, eMechanical13);
        AddExtrudedBody(Lib, Comp, 2.000, 1.250, 0.700, $303030);
    finally
        PCBServer.PostProcess;
    end;
end;

procedure BuildCapacitorFootprint(Lib : IPCB_Library);
var
    Comp : IPCB_LibComponent;
begin
    Comp := NewFootprint(Lib, 'TB_C_0805_2012M',
        '0805 MLCC, IPC-7351 nominal, 2.0 x 1.25 mm body; M1 courtyard, M13 assembly');
    if Comp = nil then Exit;
    PCBServer.PreProcess;
    try
        AddSmdPad(Lib, Comp, -0.950, 0, 1.000, 1.450, '1');
        AddSmdPad(Lib, Comp,  0.950, 0, 1.000, 1.450, '2');
        AddPcbTrack(Lib, Comp, -0.261252, -0.735, 0.261252, -0.735, 0.120, eTopOverlay);
        AddPcbTrack(Lib, Comp, -0.261252,  0.735, 0.261252,  0.735, 0.120, eTopOverlay);
        AddPcbBox(Lib, Comp, -1.700, -0.980, 1.700, 0.980, 0.050, eMechanical1);
        AddPcbBox(Lib, Comp, -1.000, -0.625, 1.000, 0.625, 0.100, eMechanical13);
        AddExtrudedBody(Lib, Comp, 2.000, 1.250, 1.450, $C8A96A);
    finally
        PCBServer.PostProcess;
    end;
end;

procedure GeneratePcbLib(const OutputDir : String);
var
    Doc : IServerDocument;
    Lib : IPCB_Library;
    DefaultComp : IPCB_LibComponent;
begin
    Doc := CreateNewDocumentFromDocumentKind('PCBLIB');
    if Doc = nil then Exit;
    Lib := PCBServer.GetCurrentPCBLibrary;
    if Lib = nil then Exit;
    DefaultComp := Lib.CurrentComponent;
    BuildResistorFootprint(Lib);
    BuildCapacitorFootprint(Lib);
    if DefaultComp <> nil then
    begin
        Lib.DeRegisterComponent(DefaultComp);
        Lib.RemoveComponent(DefaultComp);
    end;
    Lib.Board.ViewManager_FullUpdate;
    Doc.SetModified(True);
    Doc.DoSafeChangeFileNameAndSave(OutputDir + 'Treebeard_Passives.PcbLib', 'PCBLIB');
end;

procedure RegisterSchPrimitive(Comp : ISch_Component; Prim : ISch_GraphicalObject);
begin
    Comp.AddSchObject(Prim);
    SchServer.RobotManager.SendMessage(Comp.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Prim.I_ObjectAddress);
end;

procedure AddPin(Comp : ISch_Component; X, Y, Len : Integer;
                 Orientation : TRotationBy90; Designator : String);
var
    Pin : ISch_Pin;
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
var
    Line : ISch_Line;
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
var
    Rect : ISch_Rectangle;
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

procedure AddParameter(Comp : ISch_Component; Name, Value : String;
                       X, Y : Integer; Visible : Boolean);
var
    Param : ISch_Parameter;
begin
    Param := SchServer.SchObjectFactory(eParameter, eCreate_Default);
    if Param = nil then Exit;
    Param.Name                 := Name;
    Param.Text                 := Value;
    Param.IsHidden             := not Visible;
    Param.Location             := Point(MilsToCoord(X), MilsToCoord(Y));
    Param.Orientation          := eRotate0;
    Param.FontID               := 1;
    Param.Justification        := eJustify_BottomLeft;
    Param.Color                := $000000;
    Param.OwnerPartId          := 1;
    Param.OwnerPartDisplayMode := Comp.DisplayMode;
    RegisterSchPrimitive(Comp, Param);
end;

procedure AddFootprint(Comp : ISch_Component; FootprintName : String);
var
    Impl : ISch_Implementation;
begin
    Impl := Comp.AddSchImplementation;
    if Impl = nil then Exit;
    Impl.ModelName   := FootprintName;
    Impl.ModelType   := 'PCBLIB';
    Impl.Description := 'Treebeard controlled 0805 house footprint';
    Impl.IsCurrent   := True;
    Impl.AddDataFileLink(FootprintName, '', 'PCBLib');
end;

function NewSymbol(Lib : ISch_Lib; Ref, Prefix, Description : String) : ISch_Component;
var
    Comp : ISch_Component;
begin
    Result := nil;
    Comp := SchServer.SchObjectFactory(eSchComponent, eCreate_Default);
    if Comp = nil then Exit;
    Comp.LibReference         := Ref;
    Comp.Designator.Text      := Prefix + '?';
    Comp.ComponentDescription := Description;
    Comp.PartCount            := 1;
    Comp.CurrentPartId        := 1;
    Comp.DisplayMode          := 0;
    Lib.AddSchComponent(Comp);
    Comp.LibReference := Ref;
    Comp.Designator.IsHidden := False;
    Comp.Comment.IsHidden := True;
    SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Comp.I_ObjectAddress);
    Lib.CurrentSchComponent := Comp;
    Result := Comp;
end;

procedure AddCommonBlankSourcingParameters(Comp : ISch_Component);
begin
    AddParameter(Comp, 'Manufacturer', '', 0, 0, False);
    AddParameter(Comp, 'Manufacturer Part Number', '', 0, 0, False);
    AddParameter(Comp, 'LCSC Part #', '', 0, 0, False);
    AddParameter(Comp, 'Supplier', '', 0, 0, False);
    AddParameter(Comp, 'Supplier Part Number', '', 0, 0, False);
    { 2026-09-19: was 'Commodity - fuzzy match until BOM freeze'. LCSC fuzzy matching picked
      discontinued, backordered and wrong-unit parts (10M read as 10 milliohm), so every ordered
      line is now frozen to a verified LCSC number first: see the lcsc-bom-order skill and
      hardware/procurement/lcsc_verified_parts.csv. }
    AddParameter(Comp, 'Procurement Class', 'Commodity - freeze LCSC Part # before ordering', 0, 0, False);
    AddParameter(Comp, 'Do Not Substitute', 'No', 0, 0, False);
end;

procedure AddResistorSymbol(Lib : ISch_Lib);
var
    Comp : ISch_Component;
begin
    Comp := NewSymbol(Lib, 'R_0805_1PCT_0P125W_150V', 'R',
        'Generic 0805 thick-film resistor; edit Resistance only for normal use');
    if Comp = nil then Exit;
    AddPin(Comp, 100, 0, 100, eRotate180, '1');
    AddPin(Comp, 300, 0, 100, eRotate0, '2');
    { ANSI/US resistor body: eight-segment zigzag, 200 mil long. }
    AddSchLine(Comp, 100,   0, 125,  70);
    AddSchLine(Comp, 125,  70, 150, -70);
    AddSchLine(Comp, 150, -70, 175,  70);
    AddSchLine(Comp, 175,  70, 200, -70);
    AddSchLine(Comp, 200, -70, 225,  70);
    AddSchLine(Comp, 225,  70, 250, -70);
    AddSchLine(Comp, 250, -70, 275,  70);
    AddSchLine(Comp, 275,  70, 300,   0);
    AddParameter(Comp, 'Resistance', '10k', 125, -175, True);
    AddParameter(Comp, 'Component Type', 'Resistor', 0, 0, False);
    AddParameter(Comp, 'Tolerance', '1%', 0, 0, False);
    AddParameter(Comp, 'Power Rating', '0.125W', 0, 0, False);
    AddParameter(Comp, 'Voltage Rating', '150V', 0, 0, False);
    AddParameter(Comp, 'TCR', '100ppm/C', 0, 0, False);
    AddParameter(Comp, 'Technology', 'Thick Film', 0, 0, False);
    AddParameter(Comp, 'Package', '0805 (2012 Metric)', 0, 0, False);
    AddCommonBlankSourcingParameters(Comp);
    { ' ohm' after the value: a bare '470' matched nothing on LCSC and '10M' matched a 10 milliohm
      shunt. ASCII 'ohm' rather than the Greek letter, which Altium's CSV export mangles. }
    Comp.Comment.Text := '="Resistance" + '' ohm '' + "Tolerance" + '' '' + "Power Rating" + '' '' + "Voltage Rating" + '' '' + "TCR" + '' 0805 thick-film resistor''';
    Comp.Comment.IsHidden := True;
    AddFootprint(Comp, 'TB_R_0805_2012M');
end;

procedure AddCapacitorSymbol(Lib : ISch_Lib; Ref, DefaultValue, Dielectric,
                             Voltage, Tolerance : String);
var
    Comp : ISch_Component;
begin
    Comp := NewSymbol(Lib, Ref, 'C',
        'Generic 0805 MLCC preset; edit Capacitance only, always with its F (10pF, 1nF, 1uF)');
    if Comp = nil then Exit;
    { Non-polarized capacitor with a compact 50 mil plate gap.  Pin lengths }
    { preserve the original 400 mil pin-to-pin span on the schematic grid. }
    AddPin(Comp, 175, 0, 175, eRotate180, '1');
    AddPin(Comp, 225, 0, 175, eRotate0, '2');
    AddSchLine(Comp, 175, -100, 175, 100);
    AddSchLine(Comp, 225, -100, 225, 100);
    AddParameter(Comp, 'Capacitance', DefaultValue, 125, -175, True);
    AddParameter(Comp, 'Component Type', 'Capacitor', 0, 0, False);
    AddParameter(Comp, 'Tolerance', Tolerance, 0, 0, False);
    AddParameter(Comp, 'Voltage Rating', Voltage, 0, 0, False);
    AddParameter(Comp, 'Dielectric', Dielectric, 0, 0, False);
    AddParameter(Comp, 'Technology', 'MLCC', 0, 0, False);
    AddParameter(Comp, 'Package', '0805 (2012 Metric)', 0, 0, False);
    AddCommonBlankSourcingParameters(Comp);
    Comp.Comment.Text := '="Capacitance" + '' '' + "Tolerance" + '' '' + "Voltage Rating" + '' '' + "Dielectric" + '' 0805 MLCC''';
    Comp.Comment.IsHidden := True;
    AddFootprint(Comp, 'TB_C_0805_2012M');
end;

procedure GenerateSchLib(const OutputDir : String);
var
    Doc : IServerDocument;
    Lib : ISch_Lib;
    DefaultComp : ISch_Component;
begin
    Doc := CreateNewDocumentFromDocumentKind('SCHLIB');
    if Doc = nil then Exit;
    Lib := SchServer.GetCurrentSchDocument;
    if Lib = nil then Exit;
    DefaultComp := Lib.CurrentSchComponent;
    AddResistorSymbol(Lib);
    AddCapacitorSymbol(Lib, 'C_0805_X5R_16V_10PCT', '10uF', 'X5R', '16V', '10%');
    AddCapacitorSymbol(Lib, 'C_0805_X5R_25V_10PCT', '4.7uF', 'X5R', '25V', '10%');
    AddCapacitorSymbol(Lib, 'C_0805_X7R_50V_10PCT', '100nF', 'X7R', '50V', '10%');
    AddCapacitorSymbol(Lib, 'C_0805_C0G_50V_5PCT', '10nF', 'C0G/NP0', '50V', '5%');
    if DefaultComp <> nil then Lib.RemoveSchComponent(DefaultComp);
    Lib.GraphicallyInvalidate;
    Doc.SetModified(True);
    Doc.DoSafeChangeFileNameAndSave(OutputDir + 'Treebeard_Passives.SchLib', 'SCHLIB');
end;

procedure Run;
var
    OutputDir : String;
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
        if FileExists(OutputDir + 'Treebeard_Passives.PcbLib') and
           FileExists(OutputDir + 'Treebeard_Passives.SchLib') then
            WriteResponse('ok', 'generated Treebeard_Passives.PcbLib and Treebeard_Passives.SchLib')
        else
            WriteResponse('error', 'generation returned but one or both libraries are missing');
    except
        WriteResponse('error', 'exception during Altium library generation');
    end;
end;
