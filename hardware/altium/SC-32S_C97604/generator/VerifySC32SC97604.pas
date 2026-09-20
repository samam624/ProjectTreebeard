{ Verifies that the compiled IntLib resolves the crystal's embedded PCB model. }

const
    REQUEST_FILE = 'C:\Users\Public\treebeard_altium_sc32s\verify_request.txt';
    RESPONSE_FILE = 'C:\Users\Public\treebeard_altium_sc32s\verify_response.json';
    SYMBOL_NAME = 'SC-32S32.768kHz20PPM7pF';

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

procedure Run;
var
    Request, Response : TStringList;
    LibraryPath, ModelName : String;
    ModelCount : Integer;
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
        if Request.Count = 0 then
        begin
            Response.Text := '{"error":"empty request"}';
            Response.SaveToFile(RESPONSE_FILE);
            Exit;
        end;
        LibraryPath := Trim(Request[0]);
        ModelCount := IntegratedLibraryManager.ModelCount(
            SYMBOL_NAME, LibraryPath, 'PCBLIB');
        ModelName := '';
        if ModelCount > 0 then
            ModelName := IntegratedLibraryManager.ModelName(
                SYMBOL_NAME, LibraryPath, 'PCBLIB', 0);
        Response.Text := '{"model_count":' + IntToStr(ModelCount) +
                         ',"model_name":"' + JsonEscape(ModelName) + '"}';
        Response.SaveToFile(RESPONSE_FILE);
    finally
        Request.Free;
        Response.Free;
    end;
end;
