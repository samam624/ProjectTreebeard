{ Generates the Altium integrated library for the MPD BA4AAPC 4 x AA PC-pin battery holder.

  Source: MPD drawing BA4AAPC rev H (03/06/19), "PCB LAYOUT" plus the top and end views.
  Origin = body centre. Mounting holes on the body's centre line at y = +/-27.305 (54.61 apart).
  Pins at x = +26.82 from that line (the drawing's right end), 6.17 and 18.87 above the lower
  mounting hole, 12.70 apart.

  Polarity (INFERRED from the drawing, not measured): tracing the cell +/- markings through the
  four series cells, pack + is the OUTER pin (6.17 from the lower hole, under the bottom cell) and
  pack - the inner one; the end view's item 1 (MPD "positive contact") is the outer pin, which
  agrees. Check with a meter on a real holder before trusting a board.

  Deliberate deviations from the drawing, both for fit:
    pin hole 1.10 (drawing 1.02): the pin is 0.89 x 0.38, ~0.97 diagonal; JLC plated hole -0.08.
    mounting hole 2.40 (drawing 2.03): 2.03 does not clear the 2-56 screws MPD specifies. }

const
    BRIDGE_DIR    = 'C:\Users\Public\treebeard_altium_ba4aapc\';
    REQUEST_FILE  = 'C:\Users\Public\treebeard_altium_ba4aapc\generate_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_ba4aapc\generate_response.json';
    LIB_BASENAME  = 'MPD_BA4AAPC';
    SYMBOL_NAME   = 'BA4AAPC';
    FP_NAME       = 'MPD_BA4AAPC_4AA_PC';

    SILK_W  = 0.15;
    ASSY_W  = 0.10;
    CRT_W   = 0.05;

    BODY_W  = 57.48;
    BODY_H  = 63.09;
    BODY_Z  = 15.88;
    HOLE_Y  = 27.305;
    PIN_X   = 26.82;
    PIN_POS_Y = -21.135;
    PIN_NEG_Y = -8.435;

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

{ ---------------------------------------------------------------- PCB --- }

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

{ Plated through-hole pad (motor lead). Square = pin 1 (+). }
procedure AddThPad(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                   Xmm, Ymm, PadMm, HoleMm : Double; Name : String;
                   Square : Boolean);
var Pad : IPCB_Pad;
begin
    Pad := PCBServer.PCBObjectFactory(ePadObject, eNoDimension, eCreate_Default);
    if Pad = nil then Exit;
    Pad.Name     := Name;
    Pad.X        := Comp.X + Mm(Xmm);
    Pad.Y        := Comp.Y + Mm(Ymm);
    Pad.Mode     := ePadMode_Simple;
    Pad.Layer    := eMultiLayer;
    Pad.Plated   := True;
    Pad.HoleSize := Mm(HoleMm);
    Pad.TopXSize := Mm(PadMm);
    Pad.TopYSize := Mm(PadMm);
    Pad.MidXSize := Mm(PadMm);
    Pad.MidYSize := Mm(PadMm);
    Pad.BotXSize := Mm(PadMm);
    Pad.BotYSize := Mm(PadMm);
    if Square then
    begin
        Pad.TopShape := eRectangular;
        Pad.MidShape := eRectangular;
        Pad.BotShape := eRectangular;
    end
    else
    begin
        Pad.TopShape := eRounded;
        Pad.MidShape := eRounded;
        Pad.BotShape := eRounded;
    end;
    RegisterPcbPrimitive(Lib, Comp, Pad);
end;

{ Plated slot for a motor solder tab. Slot runs along Y (HoleRotation 90).
  Square = pin 1 (+), rectangular pad; otherwise oval. }
procedure AddSlotPad(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                     Xmm, Ymm, PadWmm, PadHmm, SlotWmm, SlotLmm : Double;
                     Name : String; Square : Boolean);
var Pad : IPCB_Pad;
begin
    Pad := PCBServer.PCBObjectFactory(ePadObject, eNoDimension, eCreate_Default);
    if Pad = nil then Exit;
    Pad.Name         := Name;
    Pad.X            := Comp.X + Mm(Xmm);
    Pad.Y            := Comp.Y + Mm(Ymm);
    Pad.Mode         := ePadMode_Simple;
    Pad.Layer        := eMultiLayer;
    Pad.Plated       := True;
    Pad.HoleType     := eSlotHole;
    Pad.HoleSize     := Mm(SlotWmm);
    Pad.HoleWidth    := Mm(SlotLmm);
    Pad.HoleRotation := 90;
    Pad.TopXSize := Mm(PadWmm);
    Pad.TopYSize := Mm(PadHmm);
    Pad.MidXSize := Mm(PadWmm);
    Pad.MidYSize := Mm(PadHmm);
    Pad.BotXSize := Mm(PadWmm);
    Pad.BotYSize := Mm(PadHmm);
    if Square then
    begin
        Pad.TopShape := eRectangular;
        Pad.MidShape := eRectangular;
        Pad.BotShape := eRectangular;
    end
    else
    begin
        Pad.TopShape := eRounded;
        Pad.MidShape := eRounded;
        Pad.BotShape := eRounded;
    end;
    RegisterPcbPrimitive(Lib, Comp, Pad);
end;

{ Non-plated mounting hole, pad = hole so no copper. }
procedure AddNpth(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                  Xmm, Ymm, HoleMm : Double);
var Pad : IPCB_Pad;
begin
    Pad := PCBServer.PCBObjectFactory(ePadObject, eNoDimension, eCreate_Default);
    if Pad = nil then Exit;
    Pad.Name     := '';
    Pad.X        := Comp.X + Mm(Xmm);
    Pad.Y        := Comp.Y + Mm(Ymm);
    Pad.Mode     := ePadMode_Simple;
    Pad.Layer    := eMultiLayer;
    Pad.Plated   := False;
    Pad.HoleSize := Mm(HoleMm);
    Pad.TopXSize := Mm(HoleMm);
    Pad.TopYSize := Mm(HoleMm);
    Pad.MidXSize := Mm(HoleMm);
    Pad.MidYSize := Mm(HoleMm);
    Pad.BotXSize := Mm(HoleMm);
    Pad.BotYSize := Mm(HoleMm);
    Pad.TopShape := eRounded;
    Pad.MidShape := eRounded;
    Pad.BotShape := eRounded;
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

procedure AddPcbArc(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                    Xmm, Ymm, Rmm, StartDeg, EndDeg, Wmm : Double; Layer : TLayer);
var Arc : IPCB_Arc;
begin
    Arc := PCBServer.PCBObjectFactory(eArcObject, eNoDimension, eCreate_Default);
    if Arc = nil then Exit;
    Arc.XCenter    := Comp.X + Mm(Xmm);
    Arc.YCenter    := Comp.Y + Mm(Ymm);
    Arc.Radius     := Mm(Rmm);
    Arc.StartAngle := StartDeg;
    Arc.EndAngle   := EndDeg;
    Arc.LineWidth  := Mm(Wmm);
    Arc.Layer      := Layer;
    RegisterPcbPrimitive(Lib, Comp, Arc);
end;

procedure AddPcbText(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                     Xmm, Ymm, Hmm : Double; S : String; Layer : TLayer);
var Txt : IPCB_Text;
begin
    Txt := PCBServer.PCBObjectFactory(eTextObject, eNoDimension, eCreate_Default);
    if Txt = nil then Exit;
    Txt.XLocation := Comp.X + Mm(Xmm);
    Txt.YLocation := Comp.Y + Mm(Ymm);
    Txt.Size      := Mm(Hmm);
    Txt.Width     := Mm(Hmm * 0.15);
    Txt.Layer     := Layer;
    Txt.Text      := S;
    RegisterPcbPrimitive(Lib, Comp, Txt);
end;

{ Points are a space-separated list "x1 y1 x2 y2 ..." in mm. }
function ParsePoints(const Pts : String) : TStringList;
begin
    Result := TStringList.Create;
    Result.Delimiter := ' ';
    Result.StrictDelimiter := True;
    Result.DelimitedText := Pts;
end;

{ Closed polyline on a layer through the given points. }
procedure AddPcbPoly(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                     const Pts : String; Wmm : Double; Layer : TLayer);
var L : TStringList; N, I, J : Integer;
begin
    L := ParsePoints(Pts);
    try
        N := L.Count div 2;
        for I := 0 to N - 1 do
        begin
            J := (I + 1) mod N;
            AddPcbTrack(Lib, Comp,
                StrToFloat(L[2*I]), StrToFloat(L[2*I+1]),
                StrToFloat(L[2*J]), StrToFloat(L[2*J+1]), Wmm, Layer);
        end;
    finally
        L.Free;
    end;
end;

{ Extruded 3D body from a point list (Count + 1-based X[]/Y[], the pattern
  already used by the SC-32S generator). }
procedure AddBodyPts(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                     const Pts : String; StandoffMm, HeightMm : Double;
                     Side : TBoardSide; Color : Integer);
var Body : IPCB_ComponentBody; Contour : IPCB_Contour;
    L : TStringList; N, I : Integer;
begin
    Body := PCBServer.PCBObjectFactory(eComponentBodyObject, eNoDimension, eCreate_Default);
    if Body = nil then Exit;
    L := ParsePoints(Pts);
    try
        N := L.Count div 2;
        Body.BodyProjection := Side;
        Body.Layer          := LayerUtils.MechanicalLayer(13);
        Body.StandoffHeight := Mm(StandoffMm);
        Body.OverallHeight  := Mm(StandoffMm + HeightMm);
        Body.BodyColor3D    := Color;
        Body.BodyOpacity3D  := 1.0;
        Contour := Body.MainContour.Replicate;
        Contour.Count := N;
        for I := 1 to N do
        begin
            Contour.X[I] := Comp.X + Mm(StrToFloat(L[2*(I-1)]));
            Contour.Y[I] := Comp.Y + Mm(StrToFloat(L[2*(I-1)+1]));
        end;
        Body.SetOutlineContour(Contour);
    finally
        L.Free;
    end;
    RegisterPcbPrimitive(Lib, Comp, Body);
end;

function CirclePts(Xmm, Ymm, Rmm : Double; Segs : Integer) : String;
var I : Integer; A : Double;
begin
    Result := '';
    for I := 0 to Segs - 1 do
    begin
        A := 2 * Pi * I / Segs;
        if Result <> '' then Result := Result + ' ';
        Result := Result + FormatFloat('0.0000', Xmm + Rmm * Cos(A)) + ' ' +
                           FormatFloat('0.0000', Ymm + Rmm * Sin(A));
    end;
end;

{ Round board cutout (routed by the fab) as a Board Cutout region. }
procedure AddRoundCutout(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                         Xmm, Ymm, Rmm : Double);
var Region : IPCB_Region; Contour : IPCB_Contour; I, Segs : Integer; A : Double;
begin
    Region := PCBServer.PCBObjectFactory(eRegionObject, eNoDimension, eCreate_Default);
    if Region = nil then Exit;
    Region.Layer := eMultiLayer;
    Region.Kind  := eRegionKind_BoardCutout;
    Segs := 72;
    Contour := Region.MainContour.Replicate;
    Contour.Count := Segs;
    for I := 1 to Segs do
    begin
        A := 2 * Pi * (I - 1) / Segs;
        Contour.X[I] := Comp.X + Mm(Xmm + Rmm * Cos(A));
        Contour.Y[I] := Comp.Y + Mm(Ymm + Rmm * Sin(A));
    end;
    Region.SetOutlineContour(Contour);
    RegisterPcbPrimitive(Lib, Comp, Region);
end;

function NewFootprint(Lib : IPCB_Library; const Name, Descr : String) : IPCB_LibComponent;
begin
    Result := PCBServer.CreatePCBLibComp;
    if Result = nil then Exit;
    Result.Name := Name;
    Result.Description := Descr;
    Lib.RegisterComponent(Result);
    Lib.CurrentComponent := Result;
end;

procedure BuildFootprint(Lib : IPCB_Library);
var Comp : IPCB_LibComponent; X1, Y1, X2, Y2 : Double;
begin
    Comp := NewFootprint(Lib, FP_NAME,
        'MPD BA4AAPC 4xAA holder, PC pins (rev H PCB layout); pin 1 = pack +');
    if Comp = nil then Exit;
    X1 := -BODY_W / 2; X2 := BODY_W / 2; Y1 := -BODY_H / 2; Y2 := BODY_H / 2;
    PCBServer.PreProcess;
    try
        AddNpth(Lib, Comp, 0,  HOLE_Y, 2.40);
        AddNpth(Lib, Comp, 0, -HOLE_Y, 2.40);
        AddThPad(Lib, Comp, PIN_X, PIN_POS_Y, 1.90, 1.10, '1', True);
        AddThPad(Lib, Comp, PIN_X, PIN_NEG_Y, 1.90, 1.10, '2', False);

        AddPcbBox(Lib, Comp, X1, Y1, X2, Y2, SILK_W, eTopOverlay);
        AddPcbText(Lib, Comp, PIN_X - 4.2, PIN_POS_Y - 0.8, 1.8, '+', eTopOverlay);
        AddPcbText(Lib, Comp, PIN_X - 4.2, PIN_NEG_Y - 0.8, 1.8, '-', eTopOverlay);
        AddPcbText(Lib, Comp, -9.0, -1.0, 1.5, '4xAA', eTopOverlay);

        AddPcbBox(Lib, Comp, X1, Y1, X2, Y2, ASSY_W, eMechanical13);
        AddPcbBox(Lib, Comp, X1 - 0.25, Y1 - 0.25, X2 + 0.25, Y2 + 0.25, CRT_W, eMechanical1);
        AddBodyPts(Lib, Comp,
            FormatFloat('0.000', X1) + ' ' + FormatFloat('0.000', Y1) + ' ' +
            FormatFloat('0.000', X2) + ' ' + FormatFloat('0.000', Y1) + ' ' +
            FormatFloat('0.000', X2) + ' ' + FormatFloat('0.000', Y2) + ' ' +
            FormatFloat('0.000', X1) + ' ' + FormatFloat('0.000', Y2),
            0, BODY_Z, eBoardSide_Top, $202020);
    finally
        PCBServer.PostProcess;
    end;
end;

procedure GeneratePcbLib(const OutputDir : String);
var Doc : IServerDocument; Lib : IPCB_Library; DefaultComp : IPCB_LibComponent;
begin
    Doc := CreateNewDocumentFromDocumentKind('PCBLIB');
    if Doc = nil then Exit;
    Lib := PCBServer.GetCurrentPCBLibrary;
    if Lib = nil then Exit;
    DefaultComp := Lib.CurrentComponent;
    BuildFootprint(Lib);
    if DefaultComp <> nil then
    begin
        Lib.DeRegisterComponent(DefaultComp);
        Lib.RemoveComponent(DefaultComp);
    end;
    Lib.Board.ViewManager_FullUpdate;
    Doc.SetModified(True);
    Doc.DoSafeChangeFileNameAndSave(OutputDir + LIB_BASENAME + '.PcbLib', 'PCBLIB');
end;

{ ---------------------------------------------------------- Schematic --- }

procedure RegisterSchPrimitive(Comp : ISch_Component; Prim : ISch_GraphicalObject);
begin
    Comp.AddSchObject(Prim);
    SchServer.RobotManager.SendMessage(Comp.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Prim.I_ObjectAddress);
end;

procedure AddPin(Comp : ISch_Component; X, Y, Len : Integer;
                 Orientation : TRotationBy90; Designator, PinName : String);
var Pin : ISch_Pin;
begin
    Pin := SchServer.SchObjectFactory(ePin, eCreate_Default);
    if Pin = nil then Exit;
    Pin.Location             := Point(MilsToCoord(X), MilsToCoord(Y));
    Pin.Orientation          := Orientation;
    Pin.PinLength            := MilsToCoord(Len);
    Pin.Electrical           := eElectricPassive;
    Pin.Designator           := Designator;
    Pin.Name                 := PinName;
    Pin.ShowDesignator       := True;
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

procedure AddSchCircle(Comp : ISch_Component; X, Y, R : Integer);
var Arc : ISch_Arc;
begin
    Arc := SchServer.SchObjectFactory(eArc, eCreate_Default);
    if Arc = nil then Exit;
    Arc.Location             := Point(MilsToCoord(X), MilsToCoord(Y));
    Arc.Radius               := MilsToCoord(R);
    Arc.StartAngle           := 0;
    Arc.EndAngle             := 360;
    Arc.LineWidth            := eSmall;
    Arc.Color                := $000000;
    Arc.OwnerPartId          := 1;
    Arc.OwnerPartDisplayMode := Comp.DisplayMode;
    RegisterSchPrimitive(Comp, Arc);
end;

procedure AddSchLabel(Comp : ISch_Component; X, Y : Integer; S : String);
var Lbl : ISch_Label;
begin
    Lbl := SchServer.SchObjectFactory(eLabel, eCreate_Default);
    if Lbl = nil then Exit;
    Lbl.Location             := Point(MilsToCoord(X), MilsToCoord(Y));
    Lbl.Text                 := S;
    Lbl.FontID               := 1;
    Lbl.Color                := $000000;
    Lbl.OwnerPartId          := 1;
    Lbl.OwnerPartDisplayMode := Comp.DisplayMode;
    RegisterSchPrimitive(Comp, Lbl);
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

procedure AddFootprint(Comp : ISch_Component; const Name, Descr : String;
                       Current : Boolean);
var Impl : ISch_Implementation;
begin
    Impl := Comp.AddSchImplementation;
    if Impl = nil then Exit;
    Impl.ModelName   := Name;
    Impl.ModelType   := 'PCBLIB';
    Impl.Description := Descr;
    Impl.IsCurrent   := Current;
    Impl.AddDataFileLink(Name, '', 'PCBLib');
end;

procedure GenerateSchLib(const OutputDir : String);
var Doc : IServerDocument; Lib : ISch_Lib; Comp, DefaultComp : ISch_Component;
begin
    Doc := CreateNewDocumentFromDocumentKind('SCHLIB');
    if Doc = nil then Exit;
    Lib := SchServer.GetCurrentSchDocument;
    if Lib = nil then Exit;
    DefaultComp := Lib.CurrentSchComponent;
    Comp := SchServer.SchObjectFactory(eSchComponent, eCreate_Default);
    if Comp = nil then Exit;
    Comp.LibReference         := SYMBOL_NAME;
    Comp.Designator.Text      := 'BT?';
    Comp.ComponentDescription := 'Battery holder, 4 x AA in series (6 V), PC pins, MPD BA4AAPC';
    Comp.PartCount            := 1;
    Comp.CurrentPartId        := 1;
    Comp.DisplayMode          := 0;
    Lib.AddSchComponent(Comp);
    Comp.LibReference := SYMBOL_NAME;
    Comp.Designator.IsHidden := False;
    Comp.Comment.Text := '4xAA BA4AAPC';
    Comp.Comment.IsHidden := False;
    SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Comp.I_ObjectAddress);
    Lib.CurrentSchComponent := Comp;

    { Multi-cell battery: pin 1 (+) top, pin 2 (-) bottom; long plates = +. }
    AddPin(Comp, 0,  100, 100, eRotate90,  '1', '+');
    AddPin(Comp, 0, -100, 100, eRotate270, '2', '-');
    AddSchLine(Comp, 0, 100, 0, 60);
    AddSchLine(Comp, -60, 60, 60, 60);
    AddSchLine(Comp, -30, 40, 30, 40);
    AddSchLine(Comp, -60, 10, 60, 10);
    AddSchLine(Comp, -30, -10, 30, -10);
    AddSchLine(Comp, 0, -10, 0, -100);
    AddSchLabel(Comp, 70, 60, '+');

    AddParameter(Comp, 'Component Type', 'Battery holder');
    AddParameter(Comp, 'Cells', '4 x AA in series');
    AddParameter(Comp, 'Nominal Voltage', '6V (alkaline)');
    AddParameter(Comp, 'Height Above Board', '15.88mm');
    AddParameter(Comp, 'Manufacturer', 'MPD (Memory Protection Devices)');
    AddParameter(Comp, 'Manufacturer Part Number', 'BA4AAPC');
    AddParameter(Comp, 'Supplier', 'Digi-Key');
    AddParameter(Comp, 'Supplier Part Number', '2439225');
    AddParameter(Comp, 'LCSC Part #', 'C6748055');
    AddParameter(Comp, 'Pin 1', 'pack + (outer pin; inferred from MPD drawing rev H, meter-check a real holder)');
    AddFootprint(Comp, FP_NAME, 'MPD BA4AAPC rev H PCB layout', True);

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
    { False = do NOT install. True installed every temp build into Altium; by 2026-11-11 the
      Components panel held five stale Kamoer_NKP copies. The build script installs the one
      repo copy instead. }
    IntegratedLibraryManager.CreateIntegratedLibrary(
        Project, OutputDir + LIB_BASENAME + '.IntLib', False);
end;

{ Close everything this generator opened, so the user's Projects panel is left as it was. }
procedure CloseGenerated(const OutputDir : String);
var Ext : TStringList; I : Integer; D : IServerDocument;
begin
    Ext := TStringList.Create;
    try
        Ext.Add('.PcbLib'); Ext.Add('.SchLib'); Ext.Add('.LibPkg');
        for I := 0 to Ext.Count - 1 do
        begin
            D := Client.GetDocumentByPath(OutputDir + LIB_BASENAME + Ext[I]);
            if D <> nil then
            begin
                D.SetModified(False);
                Client.CloseDocument(D);
            end;
        end;
    finally
        Ext.Free;
    end;
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
        CloseGenerated(OutputDir);
        if FileExists(OutputDir + LIB_BASENAME + '.IntLib') then
            WriteResponse('ok', 'generated compiled MPD_BA4AAPC.IntLib')
        else
            WriteResponse('error', 'source libraries generated but IntLib compilation failed');
    except
        WriteResponse('error', 'exception during BA4AAPC library generation');
    end;
end;
