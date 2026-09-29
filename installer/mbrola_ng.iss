; MBROLA NG - Windows installer (SAPI 5), ANALYSIS 11.7
; Copyright (c) 2026 Darko Milošević
; SPDX-License-Identifier: GPL-2.0-or-later
;
; Built by installer\build_installer.py, which stages the files and passes:
;   /DAppVersion=0.1.0  /DStage=<staging folder>  /DLanguages=,hr,en,
;
; - installs MBROLA_NG.dll + mbrola_ng_synth.exe (x64 and x86) and every
;   language (.dat); no voices are inside the installer
; - the voices are chosen on a checkbox page built from the voice catalog
;   (catalog.json, embedded; an online catalog replaces it when CatalogURL is
;   set), their licenses are shown, then they are downloaded (SHA-256
;   verified) into %ProgramData%\MBROLA NG\voices\<id>
; - only this installer registers SAPI: regsvr32 of both DLLs, whose
;   DllRegisterServer creates one voice token per installed voice
; - running the installer again = add / remove voices (unchecking removes)
; - silent: /VOICES=cr1,us2  or  /LANGS=hr,en  (default voice of each language)

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Stage
  #define Stage "build\stage"
#endif
#ifndef Languages
  #define Languages ",hr,en,"
#endif
; online voice catalog (HTTPS); empty = only the catalog embedded here
#define CatalogURL ""
; /DTestMode: developer test without admin rights - no SAPI registration,
; voices go to the folder given by /VOICEROOT=<folder>
#ifdef TestMode
  #define RegFlags ""
#else
  #define RegFlags "regserver"
#endif

[Setup]
#ifndef TestMode
AppId={{6F1D2C43-8E0B-4E57-9C1A-3B7E2D5A9F10}
#endif
AppName=MBROLA NG
AppVersion={#AppVersion}
AppVerName=MBROLA NG {#AppVersion}
AppPublisher=Darko Milošević
AppCopyright=Copyright (c) 2026 Darko Milošević
VersionInfoVersion={#AppVersion}
VersionInfoProductName=MBROLA NG
DefaultDirName={autopf}\MBROLA NG
DisableProgramGroupPage=yes
LicenseFile={#Stage}\licenses\LICENSE.txt
ArchitecturesAllowed=x86compatible x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#ifdef TestMode
PrivilegesRequired=lowest
AppId={{6F1D2C43-8E0B-4E57-9C1A-3B7E2D5A9F11}
OutputBaseFilename=MBROLA_NG-{#AppVersion}-setup-TEST
#else
PrivilegesRequired=admin
OutputBaseFilename=MBROLA_NG-{#AppVersion}-setup
#endif
MinVersion=10.0
ShowLanguageDialog=yes
LanguageDetectionMethod=uilanguage
WizardStyle=modern
OutputDir=.
Compression=lzma2/max
SolidCompression=yes
UninstallDisplayName=MBROLA NG
CloseApplications=yes
RestartIfNeededByRun=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "hr"; MessagesFile: "Croatian.isl"

[CustomMessages]
en.VoicesTitle=Voices
en.VoicesDesc=Which voices should be installed?
en.VoicesSub=Checked voices are downloaded from the official MBROLA voice repository and installed. Unchecking an installed voice removes it. A SAPI 5 voice is registered for every installed voice.
en.Installed=installed
en.LicTitle=Voice licenses
en.LicDesc=Please read the licenses of the voices that will be downloaded.
en.LicSub=Every voice has its own license, separate from the license of MBROLA NG:
en.LicAccept=I &accept the licenses of these voices
en.LicMustAccept=You must accept the licenses of the voices to continue, or go back and uncheck them.
en.Downloading=Downloading voices...
en.DownloadingDesc=Please wait while the selected voices are downloaded.
en.DownloadFailed=The voices could not be downloaded:%n%1%n%nRetry: try again. Ignore: continue without the voices that failed. Abort: go back.
en.VoiceDamaged=The downloaded voice %1 is not a valid MBROLA voice and will not be installed.
en.NoVoiceWarning=No voice is selected. MBROLA NG will not be available as a SAPI 5 voice until a voice is installed (run this installer again).%n%nContinue without voices?
en.RemoveVoices=Also delete the downloaded voices (%1)?%n%nThey are shared with the MBROLA NG NVDA add-on.
en.LangVoice=%1: %2 (%3)
en.ReadyVoices=Voices to download:
en.ReadyRemove=Voices to remove:
hr.VoicesTitle=Glasovi
hr.VoicesDesc=Koje glasove želite instalirati?
hr.VoicesSub=Označeni glasovi preuzimaju se sa službenog repozitorija MBROLA glasova i instaliraju. Odznačavanjem instaliranog glasa on se uklanja. Za svaki instalirani glas registrira se SAPI 5 glas.
hr.Installed=instaliran
hr.LicTitle=Licence glasova
hr.LicDesc=Pročitajte licence glasova koji će se preuzeti.
hr.LicSub=Svaki glas ima vlastitu licencu, odvojenu od licence programa MBROLA NG:
hr.LicAccept=&Prihvaćam licence ovih glasova
hr.LicMustAccept=Za nastavak morate prihvatiti licence glasova ili se vratiti i odznačiti ih.
hr.Downloading=Preuzimanje glasova...
hr.DownloadingDesc=Pričekajte dok se odabrani glasovi preuzimaju.
hr.DownloadFailed=Glasove nije moguće preuzeti:%n%1%n%nPonovi: pokušaj ponovno. Zanemari: nastavi bez glasova koji nisu preuzeti. Prekini: vrati se.
hr.VoiceDamaged=Preuzeti glas %1 nije ispravan MBROLA glas i neće biti instaliran.
hr.NoVoiceWarning=Nijedan glas nije odabran. MBROLA NG neće biti dostupan kao SAPI 5 glas dok se ne instalira neki glas (ponovno pokrenite ovaj instalacijski program).%n%nNastaviti bez glasova?
hr.RemoveVoices=Izbrisati i preuzete glasove (%1)?%n%nNjih koristi i MBROLA NG dodatak za NVDA.
hr.LangVoice=%1: %2 (%3)
hr.ReadyVoices=Glasovi za preuzimanje:
hr.ReadyRemove=Glasovi za uklanjanje:

[Files]
; 64-bit engine (64-bit SAPI clients) - registered by a 64-bit regsvr32
Source: "{#Stage}\x64\MBROLA_NG.dll"; DestDir: "{app}\x64"; Check: Is64BitInstallMode; Flags: ignoreversion {#RegFlags} restartreplace uninsrestartdelete
Source: "{#Stage}\x64\mbrola_ng_synth.exe"; DestDir: "{app}\x64"; Check: Is64BitInstallMode; Flags: ignoreversion restartreplace uninsrestartdelete
; 32-bit engine (32-bit SAPI clients) - on every Windows
Source: "{#Stage}\x86\MBROLA_NG.dll"; DestDir: "{app}\x86"; Flags: ignoreversion {#RegFlags} 32bit restartreplace uninsrestartdelete
Source: "{#Stage}\x86\mbrola_ng_synth.exe"; DestDir: "{app}\x86"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#Stage}\languages\*.dat"; DestDir: "{app}\languages"; Flags: ignoreversion
Source: "{#Stage}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion
; voice catalog: read by the installer only
Source: "{#Stage}\catalog.json"; Flags: dontcopy

[Code]
const
  CatalogURL = '{#CatalogURL}';
  Languages = '{#Languages}';

{ ------------------------------------------------------------------------- }
{ Minimal JSON reader: flattens a document into path/value pairs, e.g.       }
{ voices[0].files[1].urls[0] = "https://...", voices# = "5" (array length).  }
{ ------------------------------------------------------------------------- }
var
  JKeys, JVals: TArrayOfString;
  JCount, JPos: Integer;
  JText: String;

procedure JAdd(const K, V: String);
begin
  if JCount >= GetArrayLength(JKeys) then begin
    SetArrayLength(JKeys, JCount * 2 + 64);
    SetArrayLength(JVals, JCount * 2 + 64);
  end;
  JKeys[JCount] := K;
  JVals[JCount] := V;
  JCount := JCount + 1;
end;

procedure JSkipWs;
begin
  while (JPos <= Length(JText)) and ((JText[JPos] = ' ') or (JText[JPos] = #9) or
        (JText[JPos] = #10) or (JText[JPos] = #13)) do
    JPos := JPos + 1;
end;

function JString: String;
var
  C: Char;
  Code: Integer;
begin
  Result := '';
  JPos := JPos + 1;  // opening quote
  while JPos <= Length(JText) do begin
    C := JText[JPos];
    if C = '"' then begin
      JPos := JPos + 1;
      Exit;
    end;
    if C = '\' then begin
      JPos := JPos + 1;
      C := JText[JPos];
      if C = 'n' then Result := Result + #10
      else if C = 't' then Result := Result + #9
      else if C = 'r' then Result := Result + #13
      else if (C = 'b') or (C = 'f') then begin end
      else if C = 'u' then begin
        Code := StrToIntDef('$' + Copy(JText, JPos + 1, 4), 63);
        if Code < 128 then Result := Result + Chr(Code) else Result := Result + '?';
        JPos := JPos + 4;
      end else
        Result := Result + C;
    end else
      Result := Result + C;
    JPos := JPos + 1;
  end;
end;

procedure JValue(const Path: String); forward;

procedure JValue(const Path: String);
var
  C: Char;
  Key, S: String;
  N: Integer;
begin
  JSkipWs;
  if JPos > Length(JText) then Exit;
  C := JText[JPos];
  if C = '{' then begin
    JPos := JPos + 1;
    JSkipWs;
    if (JPos <= Length(JText)) and (JText[JPos] = '}') then begin
      JPos := JPos + 1;
      Exit;
    end;
    repeat
      JSkipWs;
      Key := JString;
      JSkipWs;
      JPos := JPos + 1;  // colon
      if Path = '' then JValue(Key) else JValue(Path + '.' + Key);
      JSkipWs;
      C := JText[JPos];
      JPos := JPos + 1;  // comma or closing brace
    until (C <> ',') or (JPos > Length(JText));
  end else if C = '[' then begin
    JPos := JPos + 1;
    N := 0;
    JSkipWs;
    if (JPos <= Length(JText)) and (JText[JPos] = ']') then
      JPos := JPos + 1
    else
      repeat
        JValue(Path + '[' + IntToStr(N) + ']');
        N := N + 1;
        JSkipWs;
        C := JText[JPos];
        JPos := JPos + 1;  // comma or closing bracket
      until (C <> ',') or (JPos > Length(JText));
    JAdd(Path + '#', IntToStr(N));
  end else if C = '"' then
    JAdd(Path, JString)
  else begin
    S := '';
    while (JPos <= Length(JText)) and (Pos(JText[JPos], ',}] ' + #9#10#13) = 0) do begin
      S := S + JText[JPos];
      JPos := JPos + 1;
    end;
    JAdd(Path, S);
  end;
end;

function JLoad(const FileName: String): Boolean;
var
  Lines: TArrayOfString;
  I: Integer;
begin
  Result := False;
  JCount := 0;
  SetArrayLength(JKeys, 0);
  SetArrayLength(JVals, 0);
  if not LoadStringsFromFile(FileName, Lines) then Exit;  { UTF-8 aware }
  JText := '';
  for I := 0 to GetArrayLength(Lines) - 1 do
    JText := JText + Lines[I] + #10;
  JPos := 1;
  try
    JValue('');
    Result := JCount > 0;
  except
    Result := False;
  end;
end;

function J(const Key: String): String;
var
  I: Integer;
begin
  Result := '';
  for I := 0 to JCount - 1 do
    if JKeys[I] = Key then begin
      Result := JVals[I];
      Exit;
    end;
end;

function JInt(const Key: String): Integer;
begin
  Result := StrToIntDef(J(Key), 0);
end;

function JEscape(const S: String): String;
var
  I: Integer;
begin
  Result := '';
  for I := 1 to Length(S) do
    if (S[I] = '\') or (S[I] = '"') then Result := Result + '\' + S[I]
    else Result := Result + S[I];
end;

{ ------------------------------------------------------------------------- }
{ Voices from the catalog                                                    }
{ ------------------------------------------------------------------------- }
var
  VoiceIdx: array of Integer;      { catalog index of each page item }
  VoiceWasInstalled: array of Boolean;
  VoiceSelected: array of Boolean; { state after the voices page }
  VoiceOk: array of Boolean;       { downloaded and verified }
  VoicePage: TInputOptionWizardPage;
  LicensePage: TOutputMsgMemoWizardPage;
  LicenseAccept: TNewCheckBox;
  DownloadPage: TDownloadWizardPage;
  Downloaded: Boolean;

function VoicesRoot: String;
begin
#ifdef TestMode
  Result := ExpandConstant('{param:VOICEROOT}');
#else
  Result := ExpandConstant('{commonappdata}\MBROLA NG\voices');
#endif
end;

function VId(I: Integer): String;
begin
  Result := J('voices[' + IntToStr(VoiceIdx[I]) + '].id');
end;

function VKey(I: Integer; const Field: String): String;
begin
  Result := 'voices[' + IntToStr(VoiceIdx[I]) + '].' + Field;
end;

function Localized(const Prefix: String): String;
begin
  Result := J(Prefix + '.' + ActiveLanguage);
  if Result = '' then Result := J(Prefix + '.en');
end;

function LanguageIndex(const Code: String): Integer;
var
  I: Integer;
begin
  Result := -1;
  for I := 0 to JInt('languages#') - 1 do
    if J('languages[' + IntToStr(I) + '].code') = Code then begin
      Result := I;
      Exit;
    end;
end;

function VoiceSize(I: Integer): Integer;
var
  F: Integer;
begin
  Result := 0;
  for F := 0 to JInt(VKey(I, 'files#')) - 1 do
    Result := Result + JInt(VKey(I, 'files[' + IntToStr(F) + '].size'));
end;

function IsInstalled(const Id: String): Boolean;
begin
  Result := FileExists(VoicesRoot + '\' + Id + '\' + Id);
end;

function ListParam(const Name: String): String;
begin
  Result := ExpandConstant('{param:' + Name + '|}');
  if Result <> '' then Result := ',' + Lowercase(Result) + ',';
end;

function LoadCatalog: Boolean;
var
  Online: String;
begin
  Result := False;
  if CatalogURL <> '' then
    try
      DownloadTemporaryFile(CatalogURL, 'catalog-online.json', '', nil);
      Online := ExpandConstant('{tmp}\catalog-online.json');
      Result := JLoad(Online) and (J('schema') = '1');
    except
      Log('online catalog not available: ' + GetExceptionMessage);
    end;
  if not Result then begin
    ExtractTemporaryFile('catalog.json');
    Result := JLoad(ExpandConstant('{tmp}\catalog.json')) and (J('schema') = '1');
  end;
end;

procedure BuildVoiceList;
var
  I, N, L: Integer;
  Lang: String;
begin
  N := 0;
  for I := 0 to JInt('voices#') - 1 do begin
    Lang := J('voices[' + IntToStr(I) + '].language');
    { only voices whose language is part of this installer }
    if Pos(',' + Lang + ',', Languages) = 0 then Continue;
    L := LanguageIndex(Lang);
    if L < 0 then Continue;
    SetArrayLength(VoiceIdx, N + 1);
    VoiceIdx[N] := I;
    N := N + 1;
  end;
  SetArrayLength(VoiceWasInstalled, N);
  SetArrayLength(VoiceSelected, N);
  SetArrayLength(VoiceOk, N);
end;

function VoiceCaption(I: Integer): String;
var
  Lang, Extra: String;
begin
  Lang := J(VKey(I, 'language'));
  if VoiceWasInstalled[I] then
    Extra := CustomMessage('Installed')
  else
    Extra := Format('%.1f MB', [VoiceSize(I) / 1048576.0]);
  Result := FmtMessage(CustomMessage('LangVoice'), [Localized('languages[' + IntToStr(LanguageIndex(Lang)) +
    '].names'), Localized(VKey(I, 'names')), Extra]);
end;

{ default voice of a language = first voice listed for it in the catalog }
function IsDefaultVoice(I: Integer): Boolean;
var
  L: Integer;
begin
  L := LanguageIndex(J(VKey(I, 'language')));
  Result := (L >= 0) and (J('languages[' + IntToStr(L) + '].voices[0]') = VId(I));
end;

function IsInstalledAny: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to GetArrayLength(VoiceWasInstalled) - 1 do
    if VoiceWasInstalled[I] then Result := True;
end;

function InitialCheck(I: Integer): Boolean;
var
  Voices, Langs: String;
begin
  Voices := ListParam('VOICES');
  Langs := ListParam('LANGS');
  if (Voices <> '') or (Langs <> '') then
    Result := (Pos(',' + VId(I) + ',', Voices) > 0) or
              (IsDefaultVoice(I) and (Pos(',' + J(VKey(I, 'language')) + ',', Langs) > 0))
  else if VoiceWasInstalled[I] then
    Result := True
  else if IsInstalledAny then
    Result := False
  else
    { first installation: the Croatian voice and the default voice of the setup language }
    Result := IsDefaultVoice(I) and ((J(VKey(I, 'language')) = 'hr') or (J(VKey(I, 'language')) = ActiveLanguage));
end;

{ ------------------------------------------------------------------------- }
{ installed.json (ANALYSIS 10.6) of the machine-wide voices                  }
{ ------------------------------------------------------------------------- }
var
  OldIds, OldInstalled, OldSha: TArrayOfString;

procedure ReadOldRegistry;
var
  I, N: Integer;
begin
  N := 0;
  if JLoad(VoicesRoot + '\installed.json') then
    for I := 0 to JInt('voices#') - 1 do begin
      SetArrayLength(OldIds, N + 1);
      SetArrayLength(OldInstalled, N + 1);
      SetArrayLength(OldSha, N + 1);
      OldIds[N] := J('voices[' + IntToStr(I) + '].id');
      OldInstalled[N] := J('voices[' + IntToStr(I) + '].installed');
      OldSha[N] := J('voices[' + IntToStr(I) + '].sha256');
      N := N + 1;
    end;
end;

function OldIndex(const Id: String): Integer;
var
  I: Integer;
begin
  Result := -1;
  for I := 0 to GetArrayLength(OldIds) - 1 do
    if OldIds[I] = Id then Result := I;
end;

function DatabaseSha(I: Integer): String;
var
  F: Integer;
begin
  Result := '';
  for F := 0 to JInt(VKey(I, 'files#')) - 1 do
    if J(VKey(I, 'files[' + IntToStr(F) + '].name')) = VId(I) then
      Result := J(VKey(I, 'files[' + IntToStr(F) + '].sha256'));
end;

procedure WriteRegistry;
var
  Lines: TArrayOfString;
  I, K, N: Integer;
  Id, When, Sha: String;
begin
  SetArrayLength(Lines, 2);
  Lines[0] := '{';
  Lines[1] := ' "voices": [';
  N := 2;
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do begin
    Id := VId(I);
    if not IsInstalled(Id) then Continue;
    K := OldIndex(Id);
    Sha := DatabaseSha(I);
    if VoiceOk[I] or (K < 0) then
      When := GetDateTimeString('yyyy/mm/dd"T"hh:nn:ss', '-', ':')
    else begin
      When := OldInstalled[K];
      if OldSha[K] <> '' then Sha := OldSha[K];
    end;
    if N > 2 then Lines[N - 1] := Lines[N - 1] + ',';
    SetArrayLength(Lines, N + 1);
    Lines[N] := '  {"id": "' + JEscape(Id) + '", "language": "' + JEscape(J(VKey(I, 'language'))) +
      '", "version": "' + JEscape(J(VKey(I, 'version'))) + '", "path": "' + JEscape(VoicesRoot + '\' + Id) +
      '", "sha256": "' + JEscape(Sha) + '", "installed": "' + JEscape(When) + '"}';
    N := N + 1;
  end;
  SetArrayLength(Lines, N + 2);
  Lines[N] := ' ]';
  Lines[N + 1] := '}';
  ForceDirectories(VoicesRoot);
  SaveStringsToUTF8FileWithoutBOM(VoicesRoot + '\installed.json', Lines, False);
end;

{ <id>.voice (ANALYSIS 10.3): the keys the NVDA Voice Manager writes, plus  }
{ sapi_lcid for the SAPI voice token                                         }
procedure WriteVoiceFile(I: Integer);
var
  Lines: TArrayOfString;
  Id, Prefix, Map: String;
  K, L, N: Integer;
begin
  Id := VId(I);
  L := LanguageIndex(J(VKey(I, 'language')));
  Map := '';
  Prefix := VKey(I, 'voice_config.phoneme_map.');
  for K := 0 to JCount - 1 do
    if Copy(JKeys[K], 1, Length(Prefix)) = Prefix then begin
      if Map <> '' then Map := Map + ' ';
      Map := Map + Copy(JKeys[K], Length(Prefix) + 1, MaxInt) + '=' + JVals[K];
    end;
  SetArrayLength(Lines, 13);
  Lines[0] := '# MBROLA NG voice description - written by the MBROLA NG installer';
  Lines[1] := 'id = ' + Id;
  Lines[2] := 'database = ' + Id;
  Lines[3] := 'language = ' + J(VKey(I, 'language'));
  Lines[4] := 'version = ' + J(VKey(I, 'version'));
  Lines[5] := 'gender = ' + J(VKey(I, 'gender'));
  Lines[6] := 'age = ' + J(VKey(I, 'age'));
  Lines[7] := 'base_pitch = ' + J(VKey(I, 'voice_config.base_pitch'));
  Lines[8] := 'pitch_range = ' + J(VKey(I, 'voice_config.pitch_range'));
  Lines[9] := 'rate_factor = ' + J(VKey(I, 'voice_config.rate_factor'));
  Lines[10] := 'volume = ' + J(VKey(I, 'voice_config.volume'));
  Lines[11] := 'phoneme_map = ' + Map;
  Lines[12] := 'sapi_lcid = ' + J('languages[' + IntToStr(L) + '].sapi_lcid');
  N := 13;
  Prefix := VKey(I, 'names.');
  for K := 0 to JCount - 1 do
    if Copy(JKeys[K], 1, Length(Prefix)) = Prefix then begin
      SetArrayLength(Lines, N + 1);
      Lines[N] := 'name_' + Copy(JKeys[K], Length(Prefix) + 1, MaxInt) + ' = ' + JVals[K];
      N := N + 1;
    end;
  SaveStringsToUTF8FileWithoutBOM(VoicesRoot + '\' + Id + '\' + Id + '.voice', Lines, False);
end;

{ ------------------------------------------------------------------------- }
{ Wizard                                                                     }
{ ------------------------------------------------------------------------- }
function IsNew(I: Integer): Boolean;
begin
  Result := VoiceSelected[I] and not VoiceWasInstalled[I];
end;

function AnyNew: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do
    if IsNew(I) then Result := True;
end;

procedure SyncSelection;
var
  I: Integer;
begin
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do
    VoiceSelected[I] := VoicePage.Values[I];
end;

procedure InitializeWizard;
var
  I: Integer;
begin
  ReadOldRegistry;
  if not LoadCatalog then
    Log('no voice catalog');
  BuildVoiceList;
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do
    VoiceWasInstalled[I] := IsInstalled(VId(I));

  VoicePage := CreateInputOptionPage(wpSelectDir, CustomMessage('VoicesTitle'), CustomMessage('VoicesDesc'),
    CustomMessage('VoicesSub'), False, False);
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do begin
    VoicePage.Add(VoiceCaption(I));
    VoicePage.Values[I] := InitialCheck(I);
  end;
  SyncSelection;

  LicensePage := CreateOutputMsgMemoPage(VoicePage.ID, CustomMessage('LicTitle'), CustomMessage('LicDesc'),
    CustomMessage('LicSub'), '');
  LicensePage.RichEditViewer.Height := LicensePage.RichEditViewer.Height - ScaleY(28);
  LicenseAccept := TNewCheckBox.Create(LicensePage);
  LicenseAccept.Parent := LicensePage.Surface;
  LicenseAccept.Caption := CustomMessage('LicAccept');
  LicenseAccept.Left := 0;
  LicenseAccept.Width := LicensePage.SurfaceWidth;
  LicenseAccept.Top := LicensePage.RichEditViewer.Top + LicensePage.RichEditViewer.Height + ScaleY(8);
  LicenseAccept.Height := ScaleY(20);

  DownloadPage := CreateDownloadPage(CustomMessage('Downloading'), CustomMessage('DownloadingDesc'), nil);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = LicensePage.ID) and not AnyNew;
end;

procedure FillLicenseText;
var
  I: Integer;
  T: String;
begin
  T := '';
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do
    if IsNew(I) then
      T := T + Localized(VKey(I, 'names')) + #13#10 + Localized(VKey(I, 'license.summary')) + #13#10 +
        J(VKey(I, 'files[0].urls[0]')) + #13#10#13#10;
  LicensePage.RichEditViewer.Lines.Text := T;
  LicenseAccept.Checked := False;
end;

function VerifyDatabase(const FileName: String): Boolean;
var
  Data: AnsiString;
begin
  Result := LoadStringFromFile(FileName, Data) and (Copy(Data, 1, 6) = 'MBROLA');
end;

function TempName(I, F: Integer): String;
begin
  Result := VId(I) + '__' + J(VKey(I, 'files[' + IntToStr(F) + '].name'));
end;

function DownloadVoices: Boolean;
var
  I, F, Answer: Integer;
  Done, Ok: Boolean;
begin
  Result := True;
  DownloadPage.Clear;
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do begin
    VoiceOk[I] := False;
    if IsNew(I) then
      for F := 0 to JInt(VKey(I, 'files#')) - 1 do
        DownloadPage.Add(J(VKey(I, 'files[' + IntToStr(F) + '].urls[0]')), TempName(I, F),
          J(VKey(I, 'files[' + IntToStr(F) + '].sha256')));
  end;
  if not AnyNew then Exit;
  DownloadPage.Show;
  try
    Done := False;
    repeat
      try
        DownloadPage.Download;
        Done := True;
      except
        if DownloadPage.AbortedByUser then
          Answer := IDABORT
        else
          Answer := SuppressibleMsgBox(FmtMessage(CustomMessage('DownloadFailed'), [GetExceptionMessage]),
            mbError, MB_ABORTRETRYIGNORE, IDIGNORE);
        if Answer = IDABORT then begin
          Result := False;
          Exit;
        end;
        Done := Answer = IDIGNORE;
      end;
    until Done;
  finally
    DownloadPage.Hide;
  end;
  { a voice is installed only when all its files arrived (SHA-256 checked }
  { by the download page) and the database is an MBROLA database          }
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do
    if IsNew(I) then begin
      Ok := True;
      for F := 0 to JInt(VKey(I, 'files#')) - 1 do
        if not FileExists(ExpandConstant('{tmp}\') + TempName(I, F)) then Ok := False;
      if Ok and not VerifyDatabase(ExpandConstant('{tmp}\') + VId(I) + '__' + VId(I)) then begin
        Ok := False;
        SuppressibleMsgBox(FmtMessage(CustomMessage('VoiceDamaged'), [VId(I)]), mbError, MB_OK, IDOK);
      end;
      VoiceOk[I] := Ok;
    end;
  Downloaded := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  I: Integer;
  Any: Boolean;
begin
  Result := True;
  if CurPageID = VoicePage.ID then begin
    SyncSelection;
    Downloaded := False;
    Any := False;
    for I := 0 to GetArrayLength(VoiceIdx) - 1 do
      if VoiceSelected[I] then Any := True;
    if not Any and (GetArrayLength(VoiceIdx) > 0) then
      Result := SuppressibleMsgBox(CustomMessage('NoVoiceWarning'), mbConfirmation, MB_YESNO, IDYES) = IDYES;
    if Result then FillLicenseText;
  end else if CurPageID = LicensePage.ID then begin
    { silent installs: naming the voices on the command line is the consent }
    if not LicenseAccept.Checked and not WizardSilent then begin
      MsgBox(CustomMessage('LicMustAccept'), mbError, MB_OK);
      Result := False;
    end;
  end else if CurPageID = wpReady then begin
    if WizardSilent then SyncSelection;
    if not Downloaded then Result := DownloadVoices;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  I: Integer;
  Add, Del: String;
begin
  Result := MemoDirInfo;
  Add := '';
  Del := '';
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do begin
    if IsNew(I) then Add := Add + Space + Localized(VKey(I, 'names')) + NewLine;
    if VoiceWasInstalled[I] and not VoiceSelected[I] then Del := Del + Space + Localized(VKey(I, 'names')) + NewLine;
  end;
  if Add <> '' then Result := Result + NewLine + NewLine + CustomMessage('ReadyVoices') + NewLine + Add;
  if Del <> '' then Result := Result + NewLine + NewLine + CustomMessage('ReadyRemove') + NewLine + Del;
end;

{ The voices are put in place before the files are copied, so that         }
{ DllRegisterServer (run after the copy) registers exactly these voices.   }
procedure CurStepChanged(CurStep: TSetupStep);
var
  I, F: Integer;
  Dir, Name: String;
begin
  if CurStep <> ssInstall then Exit;
  for I := 0 to GetArrayLength(VoiceIdx) - 1 do begin
    Dir := VoicesRoot + '\' + VId(I);
    if VoiceWasInstalled[I] and not VoiceSelected[I] then begin
      Log('removing voice ' + VId(I));
      DelTree(Dir, True, True, True);
    end else if VoiceOk[I] then begin
      Log('installing voice ' + VId(I) + ' into ' + Dir);
      if not ForceDirectories(Dir) then
        Log('could not create ' + Dir);
      for F := 0 to JInt(VKey(I, 'files#')) - 1 do begin
        Name := J(VKey(I, 'files[' + IntToStr(F) + '].name'));
        if not FileCopy(ExpandConstant('{tmp}\') + TempName(I, F), Dir + '\' + Name, False) then
          Log('could not copy ' + Name);
      end;
      WriteVoiceFile(I);
    end;
  end;
  WriteRegistry;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Root: String;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  Root := ExpandConstant('{commonappdata}\MBROLA NG');
  if DirExists(Root) then
    if SuppressibleMsgBox(FmtMessage(CustomMessage('RemoveVoices'), [Root + '\voices']), mbConfirmation,
         MB_YESNO, IDNO) = IDYES then
      DelTree(Root, True, True, True);
end;
