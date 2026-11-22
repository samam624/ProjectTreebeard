{ Generates a ready-to-install Altium integrated library for Seiko C97604. }

const
    BRIDGE_DIR   = '__OUTPUT__';
    REQUEST_FILE = '__REQUEST__';
    RESPONSE_FILE = '__RESPONSE__';
    LIB_BASENAME = 'Pump_Controller';


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

procedure AddExtrudedBody(Lib : IPCB_Library; Comp : IPCB_LibComponent; W,H,Z : Double);
var Body : IPCB_ComponentBody; Contour : IPCB_Contour;
begin
    Body := PCBServer.PCBObjectFactory(eComponentBodyObject,eNoDimension,eCreate_Default);
    Body.BodyProjection := eBoardSide_Top;
    Body.Layer := LayerUtils.MechanicalLayer(13);
    Body.StandoffHeight := 0; Body.OverallHeight := Mm(Z);
    Body.BodyColor3D := $606060; Body.BodyOpacity3D := 1.0;
    Contour := Body.MainContour.Replicate; Contour.Count := 4;
    Contour.X[1] := Comp.X-Mm(W/2); Contour.Y[1] := Comp.Y-Mm(H/2);
    Contour.X[2] := Comp.X+Mm(W/2); Contour.Y[2] := Comp.Y-Mm(H/2);
    Contour.X[3] := Comp.X+Mm(W/2); Contour.Y[3] := Comp.Y+Mm(H/2);
    Contour.X[4] := Comp.X-Mm(W/2); Contour.Y[4] := Comp.Y+Mm(H/2);
    Body.SetOutlineContour(Contour); RegisterPcbPrimitive(Lib,Comp,Body);
end;

procedure AddHole(Lib : IPCB_Library; Comp : IPCB_LibComponent; X,Y,Size,Drill : Double; Name : String; Plated : Boolean);
var Pad : IPCB_Pad;
begin
    Pad := PCBServer.PCBObjectFactory(ePadObject,eNoDimension,eCreate_Default);
    Pad.Name := Name; Pad.X := Comp.X+Mm(X); Pad.Y := Comp.Y+Mm(Y);
    Pad.Mode := ePadMode_Simple; Pad.Layer := eMultiLayer;
    Pad.HoleSize := Mm(Drill); Pad.Plated := Plated;
    Pad.TopXSize := Mm(Size); Pad.TopYSize := Mm(Size);
    Pad.MidXSize := Mm(Size); Pad.MidYSize := Mm(Size);
    Pad.BotXSize := Mm(Size); Pad.BotYSize := Mm(Size);
    Pad.TopShape := eRounded; Pad.MidShape := eRounded; Pad.BotShape := eRounded;
    RegisterPcbPrimitive(Lib,Comp,Pad);
end;

procedure GeneratePcbLib(const OutputDir : String);
var Doc : IServerDocument; Lib : IPCB_Library; Comp,DefaultComp : IPCB_LibComponent;
begin
Doc := CreateNewDocumentFromDocumentKind('PCBLIB');
Lib := PCBServer.GetCurrentPCBLibrary; DefaultComp := Lib.CurrentComponent;
PCBServer.PreProcess;
try
Comp := PCBServer.CreatePCBLibComp;
Comp.Name := 'TB_TPS709_DBV0005A';
Comp.Description := '3.3V 150mA 30V low-Iq LDO; DBV non-A/B pinout';
Lib.RegisterComponent(Comp); Lib.CurrentComponent := Comp;
AddSmdPad(Lib,Comp,-1.3, 0.95, 1.1, 0.6,'1');
AddSmdPad(Lib,Comp,-1.3, 0, 1.1, 0.6,'2');
AddSmdPad(Lib,Comp,-1.3, -0.95, 1.1, 0.6,'3');
AddSmdPad(Lib,Comp,1.3, -0.95, 1.1, 0.6,'4');
AddSmdPad(Lib,Comp,1.3, 0.95, 1.1, 0.6,'5');
AddPcbBox(Lib,Comp,-2.1, -1.75, 2.1, 1.75,0.05,eMechanical1);
AddPcbBox(Lib,Comp,-0.8,-1.45,0.8,1.45,0.1,eMechanical13);
AddExtrudedBody(Lib,Comp,1.6,2.9,1.45);
AddPcbTrack(Lib,Comp,-0.55, -1.5, 0.55, -1.5,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-0.55, 1.5, 0.55, 1.5,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-2, 1.45, -1.7, 1.45,0.12,eTopOverlay);
Comp := PCBServer.CreatePCBLibComp;
Comp.Name := 'TB_AOS_SOT23_PO00001M';
Comp.Description := '30V N-channel MOSFET; G=1 S=2 D=3';
Lib.RegisterComponent(Comp); Lib.CurrentComponent := Comp;
AddSmdPad(Lib,Comp,-0.95, -1.2, 0.8, 0.8,'1');
AddSmdPad(Lib,Comp,0.95, -1.2, 0.8, 0.8,'2');
AddSmdPad(Lib,Comp,0, 1.2, 0.8, 0.8,'3');
AddPcbBox(Lib,Comp,-1.8, -1.85, 1.8, 1.85,0.05,eMechanical1);
AddPcbBox(Lib,Comp,-1.45,-0.8,1.45,0.8,0.1,eMechanical13);
AddExtrudedBody(Lib,Comp,2.9,1.6,1.25);
AddPcbTrack(Lib,Comp,-1.6, -0.65, -1.6, 0.65,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,1.6, -0.65, 1.6, 0.65,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-1.3, -1.9, -0.6, -1.9,0.12,eTopOverlay);
Comp := PCBServer.CreatePCBLibComp;
Comp.Name := 'TB_VISHAY_SMC_SS34';
Comp.Description := '40V 3A Schottky flyback diode; SMC DO-214AB';
Lib.RegisterComponent(Comp); Lib.CurrentComponent := Comp;
AddSmdPad(Lib,Comp,-3.205, 0, 1.72, 3.2,'1');
AddSmdPad(Lib,Comp,3.205, 0, 1.72, 3.2,'2');
AddPcbBox(Lib,Comp,-4.32, -3.36, 4.32, 3.36,0.05,eMechanical1);
AddPcbBox(Lib,Comp,-3.555,-3.11,3.555,3.11,0.1,eMechanical13);
AddExtrudedBody(Lib,Comp,7.11,6.22,2.62);
AddPcbTrack(Lib,Comp,-3.6, -3.25, 3.6, -3.25,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-3.6, 3.25, 3.6, 3.25,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-3.6, -3.25, -3.6, -1.9,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-3.6, 1.9, -3.6, 3.25,0.12,eTopOverlay);
Comp := PCBServer.CreatePCBLibComp;
Comp.Name := 'TB_MPD_BA4AAPC_REVH';
Comp.Description := '4xAA PCB holder; layout model; terminal polarity requires sample check';
Lib.RegisterComponent(Comp); Lib.CurrentComponent := Comp;
AddHole(Lib,Comp,26.82, -21.135, 2.2, 1.02,'1',True);
AddHole(Lib,Comp,26.82, -8.435, 2.2, 1.02,'2',True);
AddHole(Lib,Comp,0, -27.305, 2.03, 2.03,'',False);
AddHole(Lib,Comp,0, 27.305, 2.03, 2.03,'',False);
AddPcbBox(Lib,Comp,-29.24, -32.045, 29.24, 32.045,0.05,eMechanical1);
AddPcbBox(Lib,Comp,-28.74,-31.545,28.74,31.545,0.1,eMechanical13);
AddExtrudedBody(Lib,Comp,57.48,63.09,15.88);
AddPcbTrack(Lib,Comp,-28.889999999999997, -31.695, 28.889999999999997, -31.695,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-28.889999999999997, 31.695, 28.889999999999997, 31.695,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-28.889999999999997, -31.695, -28.889999999999997, 31.695,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,28.889999999999997, -31.695, 28.889999999999997, 31.695,0.12,eTopOverlay);
Comp := PCBServer.CreatePCBLibComp;
Comp.Name := 'TB_CAP_RADIAL_D10_P5';
Comp.Description := 'Generic polarized radial capacitor; D<=10mm P=5mm H<=20mm; 220uF >=25V target';
Lib.RegisterComponent(Comp); Lib.CurrentComponent := Comp;
AddHole(Lib,Comp,-2.5, 0, 1.8, 0.9,'1',True);
AddHole(Lib,Comp,2.5, 0, 1.8, 0.9,'2',True);
AddPcbBox(Lib,Comp,-5.5, -5.5, 5.5, 5.5,0.05,eMechanical1);
AddPcbBox(Lib,Comp,-5.0,-5.0,5.0,5.0,0.1,eMechanical13);
AddExtrudedBody(Lib,Comp,10,10,20);
AddPcbTrack(Lib,Comp,-5.15, -5.15, 5.15, -5.15,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-5.15, 5.15, 5.15, 5.15,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-5.15, -5.15, -5.15, 5.15,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,5.15, -5.15, 5.15, 5.15,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-4, 1.5, -2.5, 1.5,0.12,eTopOverlay);
AddPcbTrack(Lib,Comp,-3.25, 0.75, -3.25, 2.25,0.12,eTopOverlay);
finally PCBServer.PostProcess; end;
if DefaultComp <> nil then begin Lib.DeRegisterComponent(DefaultComp); Lib.RemoveComponent(DefaultComp); end;
Doc.SetModified(True); Doc.DoSafeChangeFileNameAndSave(OutputDir+LIB_BASENAME+'.PcbLib','PCBLIB');
end;

procedure RegisterSchPrimitive(Comp : ISch_Component; Prim : ISch_GraphicalObject);
begin
    Comp.AddSchObject(Prim);
    SchServer.RobotManager.SendMessage(Comp.I_ObjectAddress, c_BroadCast,
                                       SCHM_PrimitiveRegistration, Prim.I_ObjectAddress);
end;

procedure AddPin(Comp : ISch_Component; X, Y, Len : Integer;
                 Orientation : TRotationBy90; Designator, PinName : String; Electrical : Integer);
var Pin : ISch_Pin;
begin
    Pin := SchServer.SchObjectFactory(ePin, eCreate_Default);
    if Pin = nil then Exit;
    Pin.Location             := Point(MilsToCoord(X), MilsToCoord(Y));
    Pin.Orientation          := Orientation;
    Pin.PinLength            := MilsToCoord(Len);
    Pin.Electrical           := Electrical;
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

procedure AddFootprint(Comp : ISch_Component; FootprintName : String);
var Impl : ISch_Implementation;
begin
    Impl := Comp.AddSchImplementation;
    if Impl = nil then Exit;
    Impl.ModelName   := FootprintName;
    Impl.ModelType   := 'PCBLIB';
    Impl.Description := 'Verified source-linked PCB model; see Source and Assembly Notes parameters';
    Impl.IsCurrent   := True;
    Impl.AddDataFileLink(FootprintName, '', 'PCBLib');
end;

procedure GenerateSchLib(const OutputDir : String);
var Doc : IServerDocument; Lib : ISch_Lib; Comp,DefaultComp : ISch_Component;
begin
Doc := CreateNewDocumentFromDocumentKind('SCHLIB');
Lib := SchServer.GetCurrentSchDocument; DefaultComp := Lib.CurrentSchComponent;
Comp := SchServer.SchObjectFactory(eSchComponent,eCreate_Default);
Comp.LibReference := 'TPS70933DBVR';
Comp.Designator.Text := 'U?';
Comp.ComponentDescription := '3.3V 150mA 30V low-Iq LDO; DBV non-A/B pinout';
Comp.PartCount := 1; Comp.CurrentPartId := 1; Comp.DisplayMode := 0;
Lib.AddSchComponent(Comp);
Comp.LibReference := 'TPS70933DBVR';
Comp.Comment.Text := 'TPS70933DBVR';
Comp.Designator.IsHidden := False; Comp.Comment.IsHidden := False;
SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress,c_BroadCast,SCHM_PrimitiveRegistration,Comp.I_ObjectAddress);
Lib.CurrentSchComponent := Comp;
AddSchRect(Comp,100,-500,500,100);
AddPin(Comp,100,0,100,eRotate180,'1','IN',eElectricPower);
AddPin(Comp,100,-100,100,eRotate180,'2','GND',eElectricPower);
AddPin(Comp,100,-200,100,eRotate180,'3','EN',eElectricInput);
AddPin(Comp,100,-300,100,eRotate180,'4','NC',eElectricPassive);
AddPin(Comp,100,-400,100,eRotate180,'5','OUT',eElectricPower);
AddParameter(Comp,'Manufacturer','Texas Instruments');
AddParameter(Comp,'Manufacturer Part Number','TPS70933DBVR');
AddParameter(Comp,'LCSC Part #','C89347');
AddParameter(Comp,'Do Not Substitute','Yes');
AddParameter(Comp,'Footprint','TB_TPS709_DBV0005A');
AddParameter(Comp,'Source','TI TPS709 Rev H; DBV0005A drawing 4214839/K 08/2024');
AddParameter(Comp,'Assembly Notes','EN may float for always-on; do not connect EN directly to 12V.');
AddParameter(Comp,'Sourcing Status','LCSC in stock verified 2026-11-11; recheck at order time');
AddParameter(Comp,'Verification Status','See validation report');
AddFootprint(Comp,'TB_TPS709_DBV0005A');
Comp := SchServer.SchObjectFactory(eSchComponent,eCreate_Default);
Comp.LibReference := 'AO3400A';
Comp.Designator.Text := 'Q?';
Comp.ComponentDescription := '30V N-channel MOSFET; G=1 S=2 D=3';
Comp.PartCount := 1; Comp.CurrentPartId := 1; Comp.DisplayMode := 0;
Lib.AddSchComponent(Comp);
Comp.LibReference := 'AO3400A';
Comp.Comment.Text := 'AO3400A';
Comp.Designator.IsHidden := False; Comp.Comment.IsHidden := False;
SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress,c_BroadCast,SCHM_PrimitiveRegistration,Comp.I_ObjectAddress);
Lib.CurrentSchComponent := Comp;
AddPin(Comp,100,0,100,eRotate180,'1','G',eElectricInput);
AddPin(Comp,300,-100,100,eRotate0,'2','S',eElectricPassive);
AddPin(Comp,300,100,100,eRotate0,'3','D',eElectricPassive);
AddSchLine(Comp,100, 0, 160, 0);
AddSchLine(Comp,160, -60, 160, 60);
AddSchLine(Comp,180, -65, 180, -35);
AddSchLine(Comp,180, -15, 180, 15);
AddSchLine(Comp,180, 35, 180, 65);
AddSchLine(Comp,180, 50, 230, 50);
AddSchLine(Comp,230, 50, 230, 100);
AddSchLine(Comp,230, 100, 300, 100);
AddSchLine(Comp,180, -50, 230, -50);
AddSchLine(Comp,230, -50, 230, -100);
AddSchLine(Comp,230, -100, 300, -100);
AddSchLine(Comp,230, -50, 230, 0);
AddSchLine(Comp,230, 0, 180, 0);
AddSchLine(Comp,180, 0, 200, 10);
AddSchLine(Comp,180, 0, 200, -10);
AddParameter(Comp,'Manufacturer','Alpha & Omega Semiconductor');
AddParameter(Comp,'Manufacturer Part Number','AO3400A');
AddParameter(Comp,'LCSC Part #','C20917');
AddParameter(Comp,'Do Not Substitute','Yes');
AddParameter(Comp,'Footprint','TB_AOS_SOT23_PO00001M');
AddParameter(Comp,'Source','AOS AO3400A Rev 3.1 July 2023; SOT23 PO-00001 version M');
AddParameter(Comp,'Assembly Notes','48mOhm max at VGS=2.5V; verify motor stall current and PCB thermal design.');
AddParameter(Comp,'Sourcing Status','LCSC in stock verified 2026-11-11; recheck at order time');
AddParameter(Comp,'Verification Status','See validation report');
AddFootprint(Comp,'TB_AOS_SOT23_PO00001M');
Comp := SchServer.SchObjectFactory(eSchComponent,eCreate_Default);
Comp.LibReference := 'SS34-E3_57T';
Comp.Designator.Text := 'D?';
Comp.ComponentDescription := '40V 3A Schottky flyback diode; SMC DO-214AB';
Comp.PartCount := 1; Comp.CurrentPartId := 1; Comp.DisplayMode := 0;
Lib.AddSchComponent(Comp);
Comp.LibReference := 'SS34-E3_57T';
Comp.Comment.Text := 'SS34-E3/57T';
Comp.Designator.IsHidden := False; Comp.Comment.IsHidden := False;
SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress,c_BroadCast,SCHM_PrimitiveRegistration,Comp.I_ObjectAddress);
Lib.CurrentSchComponent := Comp;
AddPin(Comp,100,0,100,eRotate180,'1','K',eElectricPassive);
AddPin(Comp,300,0,100,eRotate0,'2','A',eElectricPassive);
AddSchLine(Comp,100, 0, 170, 0);
AddSchLine(Comp,170, -70, 170, 70);
AddSchLine(Comp,170, 70, 190, 70);
AddSchLine(Comp,170, -70, 150, -70);
AddSchLine(Comp,170, 0, 250, 65);
AddSchLine(Comp,250, 65, 250, -65);
AddSchLine(Comp,250, -65, 170, 0);
AddSchLine(Comp,250, 0, 300, 0);
AddParameter(Comp,'Manufacturer','Vishay General Semiconductor');
AddParameter(Comp,'Manufacturer Part Number','SS34-E3/57T');
AddParameter(Comp,'LCSC Part #','C13354');
AddParameter(Comp,'Do Not Substitute','Yes');
AddParameter(Comp,'Footprint','TB_VISHAY_SMC_SS34');
AddParameter(Comp,'Source','Vishay document 88751 Rev 23-Apr-2020 page 3');
AddParameter(Comp,'Assembly Notes','Pad 1 is cathode (band), connected to pump positive. Not an SMA footprint.');
AddParameter(Comp,'Sourcing Status','LCSC in stock verified 2026-11-11; recheck at order time');
AddParameter(Comp,'Verification Status','See validation report');
AddFootprint(Comp,'TB_VISHAY_SMC_SS34');
Comp := SchServer.SchObjectFactory(eSchComponent,eCreate_Default);
Comp.LibReference := 'BA4AAPC_LAYOUT_CHECK';
Comp.Designator.Text := 'BT?';
Comp.ComponentDescription := '4xAA PCB holder; layout model; terminal polarity requires sample check';
Comp.PartCount := 1; Comp.CurrentPartId := 1; Comp.DisplayMode := 0;
Lib.AddSchComponent(Comp);
Comp.LibReference := 'BA4AAPC_LAYOUT_CHECK';
Comp.Comment.Text := 'BA4AAPC';
Comp.Designator.IsHidden := False; Comp.Comment.IsHidden := False;
SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress,c_BroadCast,SCHM_PrimitiveRegistration,Comp.I_ObjectAddress);
Lib.CurrentSchComponent := Comp;
AddSchRect(Comp,100,-200,500,100);
AddPin(Comp,100,0,100,eRotate180,'1','LOWER_CONTACT',eElectricPassive);
AddPin(Comp,100,-100,100,eRotate180,'2','UPPER_CONTACT',eElectricPassive);
AddParameter(Comp,'Manufacturer','Memory Protection Devices, Inc.');
AddParameter(Comp,'Manufacturer Part Number','BA4AAPC');
AddParameter(Comp,'LCSC Part #','C6748055');
AddParameter(Comp,'Do Not Substitute','Yes');
AddParameter(Comp,'Footprint','TB_MPD_BA4AAPC_REVH');
AddParameter(Comp,'Source','MPD BA4AAPC drawing Rev H 03/06/19 PCB LAYOUT');
AddParameter(Comp,'Assembly Notes','PCB layout orientation and physical terminal polarity must be confirmed against a sample before fabrication. Pins deliberately NOT named +/-.');
AddParameter(Comp,'Sourcing Status','LCSC out of stock 2026-11-11; alternate source Digi-Key 2439225, availability recheck required');
AddParameter(Comp,'Verification Status','Layout only - polarity/sample check required');
AddFootprint(Comp,'TB_MPD_BA4AAPC_REVH');
Comp := SchServer.SchObjectFactory(eSchComponent,eCreate_Default);
Comp.LibReference := 'C_RADIAL_D10_P5';
Comp.Designator.Text := 'C?';
Comp.ComponentDescription := 'Generic polarized radial capacitor; D<=10mm P=5mm H<=20mm; 220uF >=25V target';
Comp.PartCount := 1; Comp.CurrentPartId := 1; Comp.DisplayMode := 0;
Lib.AddSchComponent(Comp);
Comp.LibReference := 'C_RADIAL_D10_P5';
Comp.Comment.Text := '220uF >=25V';
Comp.Designator.IsHidden := False; Comp.Comment.IsHidden := False;
SchServer.RobotManager.SendMessage(Lib.I_ObjectAddress,c_BroadCast,SCHM_PrimitiveRegistration,Comp.I_ObjectAddress);
Lib.CurrentSchComponent := Comp;
AddPin(Comp,100,0,100,eRotate180,'1','+',eElectricPassive);
AddPin(Comp,300,0,100,eRotate0,'2','-',eElectricPassive);
AddSchLine(Comp,100, 0, 180, 0);
AddSchLine(Comp,180, -80, 180, 80);
AddSchLine(Comp,220, -80, 220, 80);
AddSchLine(Comp,220, 0, 300, 0);
AddSchLine(Comp,125, 60, 155, 60);
AddSchLine(Comp,140, 45, 140, 75);
AddParameter(Comp,'Manufacturer','');
AddParameter(Comp,'Manufacturer Part Number','');
AddParameter(Comp,'LCSC Part #','');
AddParameter(Comp,'Do Not Substitute','No - envelope constrained');
AddParameter(Comp,'Footprint','TB_CAP_RADIAL_D10_P5');
AddParameter(Comp,'Source','User-selected lab sourcing envelope; custom radial land pattern, not manufacturer-specific');
AddParameter(Comp,'Assembly Notes','Source 220uF >=25V, 5mm lead spacing, <=10mm diameter, <=20mm height, leads <=0.6mm. Check leakage for battery life.');
AddParameter(Comp,'Sourcing Status','Lab sourced; verify body and pitch');
AddParameter(Comp,'Verification Status','See validation report');
AddFootprint(Comp,'TB_CAP_RADIAL_D10_P5');
if DefaultComp <> nil then Lib.RemoveSchComponent(DefaultComp);
Lib.GraphicallyInvalidate; Doc.SetModified(True);
Doc.DoSafeChangeFileNameAndSave(OutputDir+LIB_BASENAME+'.SchLib','SCHLIB');
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
    OutputDir := '__OUTPUT__';
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
            WriteResponse('ok', 'generated compiled Pump_Controller.IntLib')
        else
            WriteResponse('error', 'source libraries generated but IntLib compilation failed');
    except
        WriteResponse('error', 'exception during pump library generation');
    end;
end;
