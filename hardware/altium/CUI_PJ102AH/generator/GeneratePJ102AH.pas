{ Generates the Altium integrated library for the Same Sky (formerly CUI Devices) PJ-102AH
  DC power jack: 2.0 mm centre pin, 5.5 mm plug, right angle, through hole.

  Source: CUI datasheet PJ-102AH, 10/30/2019, page 2 "PCB LAYOUT TOP VIEW", front and side views.
  Pins 1 (centre pin) and 2 (sleeve) lie on the jack axis 6.0 apart (3.0 + 3.0); pin 3 (switch)
  is 4.7 to the side, level with the midpoint, and 10.7 behind the mating face. Body 9.0 wide,
  14.4 long, 11.0 tall; the axis is 6.50 above the board.

  Footprint origin = pin 1. +Y points to the mating face (the plug goes in at y = +13.7).
  Seen from above with the mating face at the top, pin 3 is to the LEFT (x = -4.7), as drawn
  in CUI's top view.

  Deliberate deviation: CUI recommends 1.00 x 1.60 holes for the blade pins. JLCPCB makes plated
  slots only with length >= 2 x width, so each pin gets a round 1.6 mm plated hole instead
  (it clears a 1.00-wide blade; the same hole KiCad's BarrelJack_CUI_PJ-102AH_Horizontal uses).

  Pins (datasheet schematic): 1 = centre pin (+ for a centre-positive adapter), 2 = sleeve,
  3 = switch, closed to pin 2 with no plug inserted, open with a plug in. }

const
    BRIDGE_DIR    = 'C:\Users\Public\treebeard_altium_pj102ah\';
    REQUEST_FILE  = 'C:\Users\Public\treebeard_altium_pj102ah\generate_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_pj102ah\generate_response.json';
    LIB_BASENAME  = 'CUI_PJ102AH';
    SYMBOL_NAME   = 'PJ-102AH';
    FP_NAME       = 'CUI_PJ-102AH_DC_JACK_RA';

    SILK_W  = 0.15;
    ASSY_W  = 0.10;
    CRT_W   = 0.05;

    BODY_W     = 9.0;
    BODY_FRONT = 13.7;   { mating face: pin 3 row (y = 3.0) + 10.7 }
    BODY_REAR  = -0.7;   { 13.7 - 14.4 }
    BODY_Z     = 11.0;
    PIN2_Y     = 6.0;
    PIN3_X     = -4.7;
    PIN3_Y     = 3.0;
    HOLE       = 1.60;
    PAD        = 2.60;

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
var Comp : IPCB_LibComponent; X1, X2 : Double;
begin
    Comp := NewFootprint(Lib, FP_NAME,
        'Same Sky/CUI PJ-102AH DC jack, 2.0 mm pin, right angle THT (datasheet 10/30/2019); ' +
        'pin 1 = centre, 2 = sleeve, 3 = switch; mating face at +Y');
    if Comp = nil then Exit;
    X1 := -BODY_W / 2; X2 := BODY_W / 2;
    PCBServer.PreProcess;
    try
        AddThPad(Lib, Comp, 0, 0, PAD, HOLE, '1', True);
        AddThPad(Lib, Comp, 0, PIN2_Y, PAD, HOLE, '2', False);
        AddThPad(Lib, Comp, PIN3_X, PIN3_Y, PAD, HOLE, '3', False);

        { Silk: body outline with gaps where it would cross pad 1 (rear edge) and pad 3 (left
          edge); the mating face is a double line so the board edge is obvious. }
        AddPcbTrack(Lib, Comp, X1, BODY_REAR, -1.7, BODY_REAR, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, 1.7, BODY_REAR, X2, BODY_REAR, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, X2, BODY_REAR, X2, BODY_FRONT, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, X1, BODY_FRONT, X2, BODY_FRONT, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, X1, BODY_FRONT - 0.4, X2, BODY_FRONT - 0.4, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, X1, BODY_FRONT, X1, PIN3_Y + 1.7, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, X1, PIN3_Y - 1.7, X1, BODY_REAR, SILK_W, eTopOverlay);
        AddPcbText(Lib, Comp, 1.8, -0.4, 1.2, '+', eTopOverlay);
        AddPcbText(Lib, Comp, -3.0, 10.0, 1.0, 'PLUG', eTopOverlay);

        AddPcbBox(Lib, Comp, X1, BODY_REAR, X2, BODY_FRONT, ASSY_W, eMechanical13);
        AddPcbBox(Lib, Comp, PIN3_X - PAD / 2 - 0.25, -PAD / 2 - 0.25,
                  X2 + 0.25, BODY_FRONT + 0.25, CRT_W, eMechanical1);
        AddBodyPts(Lib, Comp,
            FormatFloat('0.000', X1) + ' ' + FormatFloat('0.000', BODY_REAR) + ' ' +
            FormatFloat('0.000', X2) + ' ' + FormatFloat('0.000', BODY_REAR) + ' ' +
            FormatFloat('0.000', X2) + ' ' + FormatFloat('0.000', BODY_FRONT) + ' ' +
            FormatFloat('0.000', X1) + ' ' + FormatFloat('0.000', BODY_FRONT),
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
    Pin.ShowName             := True;
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
    Comp.Designator.Text      := 'J?';
    Comp.ComponentDescription := 'DC power jack, 2.0 mm centre pin, 5.5 mm plug, 5 A 24 V, right angle THT, switched';
    Comp.PartCount            := 1;
    Comp.CurrentPartId        := 1;
    Comp.DisplayMode          := 0;
    Lib.AddSchComponent(Comp);
    Comp.LibReference := SYMBOL_NAME;
    Comp.Designator.IsHidden := False;
    Comp.Comment.Text := 'PJ-102AH';
    Comp.Comment.IsHidden := False;
    SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Comp.I_ObjectAddress);
    Lib.CurrentSchComponent := Comp;

    { Box with the three pins on the right, in the datasheet schematic's order 1, 3, 2. }
    AddSchLine(Comp, -300, 150, 0, 150);
    AddSchLine(Comp, 0, 150, 0, -150);
    AddSchLine(Comp, 0, -150, -300, -150);
    AddSchLine(Comp, -300, -150, -300, 150);
    AddPin(Comp, 0,  100, 200, eRotate0, '1', 'CENTER');
    AddPin(Comp, 0,    0, 200, eRotate0, '3', 'SW');
    AddPin(Comp, 0, -100, 200, eRotate0, '2', 'SLEEVE');

    AddParameter(Comp, 'Component Type', 'DC power jack');
    AddParameter(Comp, 'Center Pin', '2.0mm (mates 5.5 x 2.1 mm plugs)');
    AddParameter(Comp, 'Rated', '24VDC 5A');
    AddParameter(Comp, 'Height Above Board', '11.0mm');
    AddParameter(Comp, 'Manufacturer', 'Same Sky (formerly CUI Devices)');
    AddParameter(Comp, 'Manufacturer Part Number', 'PJ-102AH');
    AddParameter(Comp, 'Supplier', 'Digi-Key');
    AddParameter(Comp, 'Supplier Part Number', 'CP-102AH-ND');
    AddParameter(Comp, 'LCSC Part #', 'C3096093');
    AddParameter(Comp, 'Pin 1', 'centre pin = + for a centre-positive adapter');
    AddParameter(Comp, 'Pin 3', 'switch: closed to pin 2 with no plug, open with a plug in');
    AddFootprint(Comp, FP_NAME, 'CUI PJ-102AH datasheet 10/30/2019 PCB layout, round 1.6 holes', True);

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
            WriteResponse('ok', 'generated compiled CUI_PJ102AH.IntLib')
        else
            WriteResponse('error', 'source libraries generated but IntLib compilation failed');
    except
        WriteResponse('error', 'exception during PJ-102AH library generation');
    end;
end;
