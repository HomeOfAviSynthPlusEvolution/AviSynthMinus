; Core-only overlay, deliberately separate from the upstream full installer.
; Build through build-minus-mod.ps1. Never list the system core in [Files]:
; Inno must not own/delete the DLL that our uninstaller restores.
#ifndef CoreDll
  #error CoreDll must be the absolute path to the selected CI x64 AviSynth.dll
#endif
#ifndef CoreDllX86
  #error CoreDllX86 must be the absolute path to the same release's CI x86 AviSynth.dll
#endif
#define ModVersion GetFileVersionString(CoreDll)
#define NumericVersion GetVersionNumbersString(CoreDll)
#if GetFileVersionString(CoreDllX86) != ModVersion
  #error x86 and x64 DLL versions must match
#endif

[Setup]
AppId={{628B02E8-824B-4D1F-902C-354E11BF7937}
AppName=AviSynthMinus Mod (x86 + x64)
AppVersion={#ModVersion}
AppPublisher=AviSynthMinus contributors
AppPublisherURL=https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus
AppSupportURL=https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/issues
DefaultDirName={autopf}\AviSynthMinus Mod
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no
PrivilegesRequired=admin
SetupArchitecture=x64
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
LicenseFile=..\gpl.txt
InfoBeforeFile=minus-mod-info.txt
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir=..\..\build\packages\minus-mod
OutputBaseFilename=AviSynthMinus_{#ModVersion}_windows_x86-x64-mod
VersionInfoVersion={#NumericVersion}
VersionInfoProductVersion={#NumericVersion}
VersionInfoProductTextVersion={#ModVersion}
UninstallDisplayName=AviSynthMinus Mod (x86 + x64) {#ModVersion}
SetupMutex=AviSynthMinusCoreMod
Uninstallable=yes
CloseApplications=no
RestartApplications=no
ChangesAssociations=no

[Files]
Source: "{#CoreDll}"; Flags: dontcopy; DestName: "minus-core.dll"
Source: "{#CoreDllX86}"; Flags: dontcopy; DestName: "minus-core-x86.dll"
Source: "minus-mod.ps1"; Flags: dontcopy
Source: "minus-mod.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "minus-mod-info.txt"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion uninsneveruninstall
Source: "..\gpl.txt"; DestDir: "{app}"; Flags: ignoreversion

[Code]
var
  AcceptManual: Boolean;
  InstallFailed: Boolean;
  FailureMessage: String;

function RunHelper(ScriptPath, Mode, Extra: String; var Message: String): Integer;
var
  ReportPath, Args: String;
  ReportText: AnsiString;
  Code: Integer;
begin
  ReportPath := ExpandConstant('{tmp}\minus-report.txt');
  DeleteFile(ReportPath);
  Args := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
    ScriptPath + '" -Mode ' + Mode + ' -Report "' + ReportPath + '" ' + Extra;
  if not ExecWithNativeSysDir(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    Args, '', SW_HIDE, ewWaitUntilTerminated, Code) then begin
    Message := 'Cannot start Windows PowerShell: ' + SysErrorMessage(Code);
    Result := 1;
    Exit;
  end;
  Message := 'Helper failed without a report (exit ' + IntToStr(Code) + ').';
  if LoadStringFromFile(ReportPath, ReportText) then Message := UTF8Decode(ReportText);
  Log(Message);
  Result := Code;
end;

function FixedDirectory: Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(ExpandConstant('{app}')),
    RemoveBackslashUnlessRoot(ExpandConstant('{pf64}\AviSynthMinus Mod'))) = 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
  Message: String;
begin
  Result := '';
  AcceptManual := False;
  if not FixedDirectory then begin
    Result := 'This mod requires its fixed Program Files installation directory.';
    Exit;
  end;
  ExtractTemporaryFile('minus-core.dll');
  ExtractTemporaryFile('minus-core-x86.dll');
  ExtractTemporaryFile('minus-mod.ps1');
  Code := RunHelper(ExpandConstant('{tmp}\minus-mod.ps1'), 'Check',
    '-Payload "' + ExpandConstant('{tmp}\minus-core.dll') + '" -PayloadX86 "' +
    ExpandConstant('{tmp}\minus-core-x86.dll') + '"', Message);
  if Code = 10 then begin
    { Silent runs must not silently accept loss of official rollback. }
    if not WizardSilent then
      AcceptManual := MsgBox(Message + #13#10#13#10 + 'Continue with this baseline?',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
    if not AcceptManual then Result := Message;
  end else if Code <> 0 then Result := Message;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Extra, Message: String;
begin
  if CurStep = ssPostInstall then begin
    Extra := '-Payload "' + ExpandConstant('{tmp}\minus-core.dll') + '" -PayloadX86 "' +
      ExpandConstant('{tmp}\minus-core-x86.dll') + '"';
    if AcceptManual then Extra := Extra + ' -AcceptManual';
    if RunHelper(ExpandConstant('{app}\minus-mod.ps1'), 'Install', Extra, Message) <> 0 then begin
      { Keep the management/uninstall files for diagnosis and recovery.
        The helper rolls back a failed core transaction where possible. }
      InstallFailed := True;
      FailureMessage := 'The core update FAILED. Do not assume Minus was installed.' + #13#10 + Message + #13#10 +
        'Management files and any backups were retained. Close AviSynth applications and retry, or inspect the backup journal.';
      if not WizardSilent then MsgBox(FailureMessage, mbError, MB_OK);
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and InstallFailed then begin
    WizardForm.FinishedHeadingLabel.Caption := 'Core update failed';
    WizardForm.FinishedLabel.Caption := FailureMessage;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if InstallFailed then Result := 1;
end;

function InitializeUninstall: Boolean;
var
  Message: String;
begin
  Result := False;
  if not FixedDirectory then Exit;
  if RunHelper(ExpandConstant('{app}\minus-mod.ps1'), 'CheckRestore', '', Message) <> 0 then begin
    if not UninstallSilent then MsgBox(Message, mbError, MB_OK);
    Exit;
  end;
  if UninstallSilent then Result := True
  else Result := MsgBox(Message + #13#10#13#10 + 'Remove the mod management files?',
    mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Message: String;
begin
  if CurUninstallStep = usUninstall then begin
    if RunHelper(ExpandConstant('{app}\minus-mod.ps1'), 'Restore', '', Message) <> 0 then begin
      if not UninstallSilent then MsgBox(Message + #13#10 + 'Uninstall cancelled; backups retained.', mbError, MB_OK);
      Abort;
    end;
  end;
end;
