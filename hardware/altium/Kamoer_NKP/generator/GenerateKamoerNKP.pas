{ Generates a ready-to-install Altium integrated library for the Kamoer NKP
  12 V DC peristaltic pump (two-wire brushed DC motor).

  Every mechanical number comes from the Kamoer catalogue NKP page (catalogue
  page 02, "Straight" and "L plate" drawings, unit mm). Outline shapes between
  the dimensioned points were scaled off the same drawings and are
  approximate; the holes, cutout and motor diameter are the dimensioned ones.

  Footprint origin and axes
    STRAIGHT_PANEL : origin = motor axis = centre of the board cutout.
                     +Y = direction the two tube stubs point.
    LPLATE_FLAT    : origin = centre of the 4-hole pattern.
                     +Y = toward the motor's rear (terminal) end. }

const
    BRIDGE_DIR    = 'C:\Users\Public\treebeard_altium_nkp\';
    REQUEST_FILE  = 'C:\Users\Public\treebeard_altium_nkp\generate_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_nkp\generate_response.json';
    LIB_BASENAME  = 'Kamoer_NKP';
    SYMBOL_NAME   = 'Kamoer_NKP_12VDC';
    FP_VERTICAL   = 'KAMOER_NKP_VERTICAL_TABS';
    FP_PANEL      = 'KAMOER_NKP_STRAIGHT_PANEL';
    FP_LPLATE     = 'KAMOER_NKP_LPLATE_FLAT';

    SILK_W  = 0.15;
    ASSY_W  = 0.10;
    CRT_W   = 0.05;

    { Motor lead pads: 5.08 mm pitch, 1.3 mm hole. Takes a stripped 18-24 AWG
      lead directly, or a 2-way 5.08 mm screw terminal / pluggable header. }
    LEAD_PITCH = 5.08;
    LEAD_HOLE  = 1.30;
    LEAD_PAD   = 2.50;

    { VERTICAL_TABS motor-tab joint (2026-11-18). The pump head is not square
      to the motor tabs on the delivered pumps, so each tab lands in a CURVED
      slot centred on the motor axis and the pump can turn +/-30 deg to line
      the head up. JLCPCB drills plated slots (straight only) and does not
      guarantee irregular plated slots, so the arc is a ROUTED NON-PLATED slot
      (min 1.0 wide, +/-0.2) with copper arc pads around it; the tab is soldered
      to the bottom pad. The M3 standoffs carry the pump, the tabs only current. }
    TAB_R         = 11.25;  { motor axis to tab centre (scaled, 22.5 apart) }
    TAB_SLOT_W    = 1.0;    { tab ~0.5 thick }
    TAB_SLOT_HALF = 40;     { deg each side: 30 travel + 8.2 for half the 3.2 tab + 1.8 spare }
    TAB_CU_CLR    = 0.32;   { copper pull-back from a routed edge: JLCPCB min 0.2, the pump
                              board's Board Outline Clearance rule 0.3; pad corners end at 0.308 }
    TAB_CU_RING   = 1.5;    { copper band width on each side of the slot }
    { Solder mask: one opening per side follows the copper outline TAB_MASK_M
      outside it (straight ends parallel to the flush copper ends). Together with the
      pads' own rule openings it exposes the joint and the strip beside the slot but
      none of the surrounding pour. The first per-side opening (a plain +/-54 deg
      wedge, 0.3 mm oversize) exposed pour copper at the ends and edges. }
    TAB_MASK_M    = 0.05;
    TAB_PAD_HALF  = 48;     { deg; band pads at C-48 .. C+48 }
    TAB_PAD_STEP  = 3;      { deg between band pads (they overlap) }
    TAB_PAD_T     = 1.0;    { band pad length along the arc }
    { Radial bridge pad joining the two bands, ON the last band pad's angle and width so the
      arc end is flush. At 47.5 deg (first pad build) the 0.5 deg step left 0.046 mm mask
      slivers in the notches. }
    TAB_BRIDGE_A  = 48;
    TAB_BRIDGE_W  = 1.0;
    TAB_STITCH_R  = 12.75;  { stitch holes on the outer band }
    TAB_STITCH_DA = 25;     { deg, stitch holes at centre and +/- this }
    TAB_STITCH_HOLE = 0.60;
    TAB_STITCH_PAD  = 1.20;

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

function PolarPt(Rmm, Deg : Double) : String;
begin
    Result := FormatFloat('0.0000', Rmm * Cos(Deg * Pi / 180)) + ' ' +
              FormatFloat('0.0000', Rmm * Sin(Deg * Pi / 180));
end;

function JoinPt(const Acc, P : String) : String;
begin
    if Acc = '' then Result := P else Result := Acc + ' ' + P;
end;

{ Annular sector about the origin, R1..R2, angles A1..A2 (deg, A1 < A2),
  sampled at <= 1 deg. }
function SectorPts(R1, R2, A1, A2 : Double) : String;
var N, I : Integer; S : Double;
begin
    N := Trunc(A2 - A1);
    if N < A2 - A1 then N := N + 1;
    if N < 1 then N := 1;
    S := (A2 - A1) / N;
    Result := '';
    for I := 0 to N do Result := JoinPt(Result, PolarPt(R2, A1 + I * S));
    for I := N downto 0 do Result := JoinPt(Result, PolarPt(R1, A1 + I * S));
end;

{ Curved slot of width 2*HalfW whose centreline is the circle R about the
  origin from C-H to C+H deg, with round ends. Integer degrees, so the 1 deg
  arcs and 10 deg end caps hit the 0/90/180/270 extremes exactly. }
function ArcSlotPts(C, H : Integer; R, HalfW : Double) : String;
var A, D : Integer; Cx, Cy : Double;
begin
    Result := '';
    for A := C - H to C + H do Result := JoinPt(Result, PolarPt(R + HalfW, A));
    Cx := R * Cos((C + H) * Pi / 180); Cy := R * Sin((C + H) * Pi / 180);
    for D := 1 to 17 do
        Result := JoinPt(Result,
            FormatFloat('0.0000', Cx + HalfW * Cos((C + H + 10 * D) * Pi / 180)) + ' ' +
            FormatFloat('0.0000', Cy + HalfW * Sin((C + H + 10 * D) * Pi / 180)));
    for A := C + H downto C - H do Result := JoinPt(Result, PolarPt(R - HalfW, A));
    Cx := R * Cos((C - H) * Pi / 180); Cy := R * Sin((C - H) * Pi / 180);
    for D := 1 to 17 do
        Result := JoinPt(Result,
            FormatFloat('0.0000', Cx + HalfW * Cos((C - H + 180 + 10 * D) * Pi / 180)) + ' ' +
            FormatFloat('0.0000', Cy + HalfW * Sin((C - H + 180 + 10 * D) * Pi / 180)));
end;

{ Region through a point list. Kind = copper (also used on mask layers) or
  board cutout (on Multi-Layer, routed by the fab). }
procedure AddRegionPts(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                       const Pts : String; Layer : TLayer; Kind : TRegionKind);
var Region : IPCB_Region; Contour : IPCB_Contour; L : TStringList; N, I : Integer;
begin
    Region := PCBServer.PCBObjectFactory(eRegionObject, eNoDimension, eCreate_Default);
    if Region = nil then Exit;
    L := ParsePoints(Pts);
    try
        N := L.Count div 2;
        Region.Layer := Layer;
        Region.Kind  := Kind;
        Contour := Region.MainContour.Replicate;
        Contour.Count := N;
        for I := 1 to N do
        begin
            Contour.X[I] := Comp.X + Mm(StrToFloat(L[2*(I-1)]));
            Contour.Y[I] := Comp.Y + Mm(StrToFloat(L[2*(I-1)+1]));
        end;
        Region.SetOutlineContour(Contour);
    finally
        L.Free;
    end;
    RegisterPcbPrimitive(Lib, Comp, Region);
end;

{ Rectangular pad (SMD on one copper layer, or plated through-hole when
  HoleMm > 0) at polar position (Rmm, Deg), rotated so its X size runs
  radially. Every copper shape in the tab pads is a pad carrying the pin's
  name, so Altium gives all of it the pin's net. (The first build, 11-18,
  used copper REGIONS touching the stitch pads. Altium left them No Net, so
  the router was blocked and DRC reported region-to-pad shorts.) }
procedure AddPolarPad(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                      Rmm, Deg, RadialMm, TangMm, HoleMm : Double;
                      Layer : TLayer; Name : String; Round : Boolean);
var Pad : IPCB_Pad;
begin
    Pad := PCBServer.PCBObjectFactory(ePadObject, eNoDimension, eCreate_Default);
    if Pad = nil then Exit;
    Pad.Name     := Name;
    Pad.X        := Comp.X + Mm(Rmm * Cos(Deg * Pi / 180));
    Pad.Y        := Comp.Y + Mm(Rmm * Sin(Deg * Pi / 180));
    Pad.Mode     := ePadMode_Simple;
    Pad.Layer    := Layer;
    Pad.HoleSize := Mm(HoleMm);
    Pad.Plated   := True;
    Pad.TopXSize := Mm(RadialMm);
    Pad.TopYSize := Mm(TangMm);
    Pad.MidXSize := Mm(RadialMm);
    Pad.MidYSize := Mm(TangMm);
    Pad.BotXSize := Mm(RadialMm);
    Pad.BotYSize := Mm(TangMm);
    if Round then
    begin
        Pad.TopShape := eRounded;
        Pad.MidShape := eRounded;
        Pad.BotShape := eRounded;
    end
    else
    begin
        Pad.TopShape := eRectangular;
        Pad.MidShape := eRectangular;
        Pad.BotShape := eRectangular;
    end;
    Pad.Rotation := Deg;
    { Pads keep their normal (rule) mask openings. Tenting them from script did not
      work on 11-18: IsTenting_Top/Bottom read back False, and setting
      SolderMaskExpansionMode/SolderMaskExpansion crashed ScriptingSystem.dll. }
    RegisterPcbPrimitive(Lib, Comp, Pad);
end;

{ Copper arc pad on one layer around the tab slot centred at C deg: an inner
  and an outer band of overlapping rectangular pads every TAB_PAD_STEP deg,
  joined by one radial bridge pad beyond each end of the slot. }
procedure AddTabCopper(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                       C : Integer; Layer : TLayer; Name : String);
var Rin, Rout, Span : Double; A : Integer;
begin
    Rin  := TAB_R - TAB_SLOT_W / 2 - TAB_CU_CLR - TAB_CU_RING / 2;
    Rout := TAB_R + TAB_SLOT_W / 2 + TAB_CU_CLR + TAB_CU_RING / 2;
    Span := TAB_SLOT_W + 2 * (TAB_CU_CLR + TAB_CU_RING);
    A := C - TAB_PAD_HALF;
    while A <= C + TAB_PAD_HALF do
    begin
        AddPolarPad(Lib, Comp, Rin,  A, TAB_CU_RING, TAB_PAD_T, 0, Layer, Name, False);
        AddPolarPad(Lib, Comp, Rout, A, TAB_CU_RING, TAB_PAD_T, 0, Layer, Name, False);
        A := A + TAB_PAD_STEP;
    end;
    AddPolarPad(Lib, Comp, TAB_R, C + TAB_BRIDGE_A, Span, TAB_BRIDGE_W, 0, Layer, Name, False);
    AddPolarPad(Lib, Comp, TAB_R, C - TAB_BRIDGE_A, Span, TAB_BRIDGE_W, 0, Layer, Name, False);
end;

{ Mask opening for one joint: the copper outline (bands Ri..Ro, ends flush
  with the end pads' outer edges at C +/- TAB_PAD_HALF) grown by TAB_MASK_M.
  Each end is a straight line parallel to the radial at C +/- TAB_PAD_HALF,
  offset H = TAB_PAD_T/2 + TAB_MASK_M; it meets circle r at angle asin(H/r). }
function JointMaskPts(C : Integer; Ri, Ro : Double) : String;
var N, I : Integer; H, DO_, DI, A1, A2 : Double;
begin
    H := TAB_PAD_T / 2 + TAB_MASK_M;
    Ri := Ri - TAB_MASK_M;
    Ro := Ro + TAB_MASK_M;
    DO_ := ArcTan(H / Sqrt(Ro * Ro - H * H)) * 180 / Pi;
    DI  := ArcTan(H / Sqrt(Ri * Ri - H * H)) * 180 / Pi;
    N := 120;
    Result := '';
    A1 := C - TAB_PAD_HALF - DO_; A2 := C + TAB_PAD_HALF + DO_;
    for I := 0 to N do Result := JoinPt(Result, PolarPt(Ro, A1 + (A2 - A1) * I / N));
    A1 := C - TAB_PAD_HALF - DI; A2 := C + TAB_PAD_HALF + DI;
    for I := N downto 0 do Result := JoinPt(Result, PolarPt(Ri, A1 + (A2 - A1) * I / N));
end;

{ One motor tab: routed arc slot, copper arc pads top + bottom, ONE mask
  opening per side hugging the copper, and three plated stitch holes (square, rotated
  radially, on pin 1) tying top to bottom. }
procedure AddTabArc(Lib : IPCB_Library; Comp : IPCB_LibComponent;
                    C : Integer; Name : String; Square : Boolean);
var Ri1, Ro2 : Double; K : Integer;
begin
    Ri1 := TAB_R - TAB_SLOT_W / 2 - TAB_CU_CLR - TAB_CU_RING;
    Ro2 := TAB_R + TAB_SLOT_W / 2 + TAB_CU_CLR + TAB_CU_RING;
    AddRegionPts(Lib, Comp, ArcSlotPts(C, TAB_SLOT_HALF, TAB_R, TAB_SLOT_W / 2),
                 eMultiLayer, eRegionKind_BoardCutout);
    AddTabCopper(Lib, Comp, C, eTopLayer, Name);
    AddTabCopper(Lib, Comp, C, eBottomLayer, Name);
    AddRegionPts(Lib, Comp, JointMaskPts(C, Ri1, Ro2), eTopSolder, eRegionKind_Copper);
    AddRegionPts(Lib, Comp, JointMaskPts(C, Ri1, Ro2), eBottomSolder, eRegionKind_Copper);
    for K := -1 to 1 do
    begin
        AddPolarPad(Lib, Comp, TAB_STITCH_R, C + K * TAB_STITCH_DA,
                    TAB_STITCH_PAD, TAB_STITCH_PAD, TAB_STITCH_HOLE, eMultiLayer, Name,
                    not Square);
    end;
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

{ Straight plate: the PCB is the mounting panel. Plate + pump head on TOP,
  motor (dia 29 x 44) hangs through the cutout below the board. }
procedure BuildPanelFootprint(Lib : IPCB_Library);
var Comp : IPCB_LibComponent; PlatePts : String;
begin
    Comp := NewFootprint(Lib, FP_PANEL,
        'Kamoer NKP straight plate, PCB as panel: 2x M3 (dia 3.2) at 48.5 mm, ' +
        'dia 31 cutout for the dia 29 motor, motor leads to 5.08 mm pads');
    if Comp = nil then Exit;
    { Plate outline (catalogue drawing: 54.5 wide, 40.3 tall). }
    PlatePts := '-13.5 20.15 13.5 20.15 13.5 15.6 26.5 4.0 27.25 0 26.5 -4.0 ' +
                '10.5 -20.15 -10.5 -20.15 -26.5 -4.0 -27.25 0 -26.5 4.0 -13.5 15.6';
    PCBServer.PreProcess;
    try
        { Mounting: 2-dia3.2 at 48.5 +/-0.15 (dimensioned). M3 screws. }
        AddNpth(Lib, Comp, -24.25, 0, 3.2);
        AddNpth(Lib, Comp,  24.25, 0, 3.2);
        { Motor dia 29 (dimensioned) -> dia 31 cutout, 1 mm radial clearance. }
        AddRoundCutout(Lib, Comp, 0, 0, 15.5);
        { Motor leads. Pin 1 = + (the motor tab with the red mark). }
        AddThPad(Lib, Comp, -LEAD_PITCH/2, -25.5, LEAD_PAD, LEAD_HOLE, '1', True);
        AddThPad(Lib, Comp,  LEAD_PITCH/2, -25.5, LEAD_PAD, LEAD_HOLE, '2', False);

        { Silk: plate outline, cutout edge, tube direction, + mark. }
        AddPcbPoly(Lib, Comp, PlatePts, SILK_W, eTopOverlay);
        AddPcbArc(Lib, Comp, 0, 0, 15.9, 0, 360, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, -8.4, 20.6, -8.4, 33.5, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp,  8.7, 20.6,  8.7, 33.5, SILK_W, eTopOverlay);
        AddPcbText(Lib, Comp, -LEAD_PITCH/2 - 4.0, -26.4, 1.5, '+', eTopOverlay);
        AddPcbText(Lib, Comp, -6.0, 24.0, 1.2, 'TUBES', eTopOverlay);

        { Assembly (Mech 13) and courtyard (Mech 1, as SC-32S). Courtyard
          includes the tube stubs, which stick out ~13.8 mm past the plate
          edge in the board plane, 8-15 mm above the board. }
        AddPcbPoly(Lib, Comp, PlatePts, ASSY_W, eMechanical13);
        AddPcbPoly(Lib, Comp,
            '-28.0 -28.0 28.0 -28.0 28.0 21.0 12.0 21.0 12.0 35.0 -12.0 35.0 -12.0 21.0 -28.0 21.0',
            CRT_W, eMechanical1);

        { 3D: plate + head on top (prism of the plate outline, 23.5 tall),
          tube stubs, and the motor below the board. }
        AddBodyPts(Lib, Comp, PlatePts, 0, 23.5, eBoardSide_Top, $303030);
        AddBodyPts(Lib, Comp, '-10.6 20.15 -6.2 20.15 -6.2 34.0 -10.6 34.0',
                   8.6, 6.4, eBoardSide_Top, $C0E0F0);
        AddBodyPts(Lib, Comp, '6.6 20.15 11.0 20.15 11.0 34.0 6.6 34.0',
                   8.6, 6.4, eBoardSide_Top, $C0E0F0);
        AddBodyPts(Lib, Comp, CirclePts(0, 0, 14.5, 32), 0, 42.4,
                   eBoardSide_Bottom, $C8C8C8);
    finally
        PCBServer.PostProcess;
    end;
end;

{ Vertical, tabs soldered: the pump stands on its motor end. The two motor
  solder tabs go through curved routed slots (the pump turns +/-30 deg about
  its axis to square the head up) and are soldered to copper arc pads on the
  bottom; two M3 standoffs from
  the PCB to the straight plate's 2 holes carry the pump, so the tabs only
  carry current. Origin = motor axis. Tubes point +Y (flip the pump 180 deg
  about its axis to swap tube direction AND which tab lands on pin 1).

  Motor-end numbers were SCALED off the catalogue drawings (not dimensioned):
  tab spacing 22.5, tab ~3.2 wide x ~0.5 thick, tab 5.2 proud of the can
  face, bearing boss dia ~10.7 x 2.2 proud, plate to can face ~38.7.
  With 40 mm standoffs: can face ~1.3 above the board, boss tip inside the
  dia 13 hole, tab tips ~2.3 below the board's bottom face. }
procedure BuildVerticalFootprint(Lib : IPCB_Library);
var Comp : IPCB_LibComponent; PlatePts : String;
begin
    Comp := NewFootprint(Lib, FP_VERTICAL,
        'Kamoer NKP standing on its motor end: motor tabs soldered to copper arc ' +
        'pads around 1.0 mm routed arc slots at R 11.25 (+/-30 deg rotation), ' +
        '2x M3 standoffs (40 mm) to the straight plate holes at 48.5 mm, ' +
        'dia 13 hole for the bearing boss');
    if Comp = nil then Exit;
    PlatePts := '-13.5 20.15 13.5 20.15 13.5 15.6 26.5 4.0 27.25 0 26.5 -4.0 ' +
                '10.5 -20.15 -10.5 -20.15 -26.5 -4.0 -27.25 0 -26.5 4.0 -13.5 15.6';
    PCBServer.PreProcess;
    try
        { Standoff holes: same 48.5 mm as the plate (dimensioned). }
        AddNpth(Lib, Comp, -24.25, 0, 3.2);
        AddNpth(Lib, Comp,  24.25, 0, 3.2);
        { Bearing boss clearance: dia ~10.7 boss -> dia 13 routed hole. }
        AddRoundCutout(Lib, Comp, 0, 0, 6.5);
        { Motor tabs (~0.5 x 3.2): arc slots centred on the nominal tab
          positions, pin 1 at 180 deg, pin 2 at 0 deg.
          Pin 1 = + = tab beside the red dot on the motor end cap. }
        AddTabArc(Lib, Comp, 180, '1', True);
        AddTabArc(Lib, Comp,   0, '2', False);

        { Silk: motor can (dia 29), standoff hex (5.5 AF -> r 3.2), +, tubes.
          The + sits between the boss hole and pin 1's copper. }
        AddPcbArc(Lib, Comp, 0, 0, 14.9, 0, 360, SILK_W, eTopOverlay);
        AddPcbArc(Lib, Comp, -24.25, 0, 3.4, 0, 360, SILK_W, eTopOverlay);
        AddPcbArc(Lib, Comp,  24.25, 0, 3.4, 0, 360, SILK_W, eTopOverlay);
        AddPcbText(Lib, Comp, -8.6, -0.75, 1.5, '+', eTopOverlay);
        AddPcbText(Lib, Comp, -4.5, 16.2, 1.2, 'TUBES', eTopOverlay);
        { Tube-direction tick, kept clear of the TUBES text (they touched: silk-to-silk DRC). }
        AddPcbTrack(Lib, Comp, 0, 15.3, 0, 15.9, SILK_W, eTopOverlay);

        { Assembly: the plate's shadow, ~40 mm overhead. Parts may sit under
          it outside the can circle. Courtyard (Mech 1): can + standoffs. }
        AddPcbPoly(Lib, Comp, PlatePts, ASSY_W, eMechanical13);
        AddPcbArc(Lib, Comp, 0, 0, 14.5, 0, 360, ASSY_W, eMechanical13);
        AddPcbArc(Lib, Comp, 0, 0, 15.5, 0, 360, CRT_W, eMechanical1);
        AddPcbArc(Lib, Comp, -24.25, 0, 3.6, 0, 360, CRT_W, eMechanical1);
        AddPcbArc(Lib, Comp,  24.25, 0, 3.6, 0, 360, CRT_W, eMechanical1);

        { 3D: motor can 1.3..40, standoffs, plate + head 40..63.5, tubes. }
        AddBodyPts(Lib, Comp, CirclePts(0, 0, 14.5, 32), 1.3, 38.7,
                   eBoardSide_Top, $C8C8C8);
        AddBodyPts(Lib, Comp, CirclePts(-24.25, 0, 2.75, 6), 0, 40.0,
                   eBoardSide_Top, $40B0D0);
        AddBodyPts(Lib, Comp, CirclePts( 24.25, 0, 2.75, 6), 0, 40.0,
                   eBoardSide_Top, $40B0D0);
        AddBodyPts(Lib, Comp, PlatePts, 40.0, 23.5, eBoardSide_Top, $303030);
        AddBodyPts(Lib, Comp, '-10.6 20.15 -6.2 20.15 -6.2 34.0 -10.6 34.0',
                   48.6, 6.4, eBoardSide_Top, $C0E0F0);
        AddBodyPts(Lib, Comp, '6.6 20.15 11.0 20.15 11.0 34.0 6.6 34.0',
                   48.6, 6.4, eBoardSide_Top, $C0E0F0);
    finally
        PCBServer.PostProcess;
    end;
end;

{ L plate: foot screwed flat to the PCB, motor axis parallel to the board,
  ~20.7 mm up, so the motor underside is ~6 mm above the board. }
procedure BuildLPlateFootprint(Lib : IPCB_Library);
var Comp : IPCB_LibComponent;
begin
    Comp := NewFootprint(Lib, FP_LPLATE,
        'Kamoer NKP L plate, foot flat on PCB: 4x dia 4.3 at 48 and 37 mm; ' +
        'motor parallel to board; motor leads to 5.08 mm pads');
    if Comp = nil then Exit;
    PCBServer.PreProcess;
    try
        { 4-dia4.3 (dimensioned): pairs at 48 +/-0.15 and 37 +/-0.15. }
        AddNpth(Lib, Comp, -24.0, 0, 4.3);
        AddNpth(Lib, Comp, -18.5, 0, 4.3);
        AddNpth(Lib, Comp,  18.5, 0, 4.3);
        AddNpth(Lib, Comp,  24.0, 0, 4.3);
        { Leads behind the motor's terminal end (47 mm from plate face). }
        AddThPad(Lib, Comp, -LEAD_PITCH/2, 42.5, LEAD_PAD, LEAD_HOLE, '1', True);
        AddThPad(Lib, Comp,  LEAD_PITCH/2, 42.5, LEAD_PAD, LEAD_HOLE, '2', False);

        { Silk: foot ears, upright plate, head, motor overhang. Plate's
          head-side face is 8.5 mm from the hole line (dimensioned). }
        AddPcbBox(Lib, Comp, -28.0, -6.4, 28.0, 5.4, SILK_W, eTopOverlay);
        AddPcbBox(Lib, Comp, -23.0, -8.5, 23.0, -6.4, SILK_W, eTopOverlay);
        AddPcbBox(Lib, Comp, -18.2, -28.8, 18.2, -8.5, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, -14.5, 5.4, -14.5, 38.5, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp,  14.5, 5.4,  14.5, 38.5, SILK_W, eTopOverlay);
        AddPcbTrack(Lib, Comp, -14.5, 38.5, 14.5, 38.5, SILK_W, eTopOverlay);
        AddPcbText(Lib, Comp, -LEAD_PITCH/2 - 4.0, 41.6, 1.5, '+', eTopOverlay);
        AddPcbText(Lib, Comp, -5.0, 20.0, 1.2, 'MOTOR', eTopOverlay);

        AddPcbBox(Lib, Comp, -28.0, -28.8, 28.0, 5.4, ASSY_W, eMechanical13);
        AddPcbBox(Lib, Comp, -14.5, 5.4, 14.5, 38.5, ASSY_W, eMechanical13);
        AddPcbBox(Lib, Comp, -28.5, -29.3, 28.5, 44.5, CRT_W, eMechanical1);

        { 3D: foot, upright + head (40 tall), motor box, vertical tubes. }
        AddBodyPts(Lib, Comp, '-28.0 -6.4 28.0 -6.4 28.0 5.4 -28.0 5.4',
                   0, 3.2, eBoardSide_Top, $303030);
        AddBodyPts(Lib, Comp, '-23.0 -28.8 23.0 -28.8 23.0 -5.1 -23.0 -5.1',
                   0, 40.0, eBoardSide_Top, $303030);
        AddBodyPts(Lib, Comp, '-14.5 -5.1 14.5 -5.1 14.5 38.5 -14.5 38.5',
                   6.2, 29.0, eBoardSide_Top, $C8C8C8);
        AddBodyPts(Lib, Comp, CirclePts(-9.2, -16.9, 2.5, 12), 30.0, 24.0,
                   eBoardSide_Top, $C0E0F0);
        AddBodyPts(Lib, Comp, CirclePts( 9.2, -16.9, 2.5, 12), 30.0, 24.0,
                   eBoardSide_Top, $C0E0F0);
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
    BuildVerticalFootprint(Lib);
    BuildPanelFootprint(Lib);
    BuildLPlateFootprint(Lib);
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
    Comp.Designator.Text      := 'M?';
    Comp.ComponentDescription := 'Kamoer NKP peristaltic pump, 12 V DC brushed motor, 2-wire';
    Comp.PartCount            := 1;
    Comp.CurrentPartId        := 1;
    Comp.DisplayMode          := 0;
    Lib.AddSchComponent(Comp);
    Comp.LibReference := SYMBOL_NAME;
    Comp.Designator.IsHidden := False;
    Comp.Comment.Text := 'Kamoer NKP 12V';
    Comp.Comment.IsHidden := False;
    SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Comp.I_ObjectAddress);
    Lib.CurrentSchComponent := Comp;

    { Vertical motor: pin 1 (+) at top, pin 2 (-) at bottom, circled M. }
    AddPin(Comp, 0,  100, 100, eRotate90,  '1', '+');
    AddPin(Comp, 0, -100, 100, eRotate270, '2', '-');
    AddSchCircle(Comp, 0, 0, 100);
    AddSchLabel(Comp, -30, -30, 'M');
    AddSchLabel(Comp, 30, 110, '+');

    AddParameter(Comp, 'Component Type', 'Peristaltic pump, brushed DC motor');
    AddParameter(Comp, 'Rated Voltage', '12V DC');
    AddParameter(Comp, 'Rated Current', '0.25A (Kamoer catalogue, NKP DC 12 V)');
    AddParameter(Comp, 'Pump Tube', '3mm ID x 5mm OD silicone');
    AddParameter(Comp, 'Pin 1', '+ (red-marked motor tab); swap leads to reverse flow');
    AddParameter(Comp, 'Mounting', 'Default: motor tabs soldered in PCB slots; plate on OUTSIDE of enclosure wall, M3 screws through plate + wall into standoffs holding the PCB (plan 2026-11-11)');
    AddParameter(Comp, 'Mounting Hardware', '2x M3 hex standoff F-F, length 40mm minus wall thickness; 2x M3 screw, plate(~3.5)+wall+4.5mm; 2x M3x6 screw through PCB');
    AddParameter(Comp, 'Manufacturer', 'Kamoer');
    AddParameter(Comp, 'Manufacturer Part Number', 'NKP-DC-S10B');
    AddParameter(Comp, 'Supplier', 'Amazon');
    AddParameter(Comp, 'Exclude From BOM Upload', 'Yes - hand-assembled, not an LCSC part');
    AddFootprint(Comp, FP_VERTICAL,
        'Stands on motor end, tabs soldered into slots, M3 standoffs to plate', True);
    AddFootprint(Comp, FP_PANEL,
        'Straight plate, PCB as panel, motor through dia 31 cutout, wired leads', False);
    AddFootprint(Comp, FP_LPLATE,
        'L plate, foot screwed flat to PCB, motor parallel to board', False);

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
            WriteResponse('ok', 'generated compiled Kamoer_NKP.IntLib')
        else
            WriteResponse('error', 'source libraries generated but IntLib compilation failed');
    except
        WriteResponse('error', 'exception during Kamoer NKP library generation');
    end;
end;
