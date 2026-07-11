# Windows GUI packaging

Every Windows artifact uses one directory layout:

```text
vicon-lsl-bridge-gui.exe
vicon-lsl-bridge.exe
lsl*.dll
platforms/qwindows.dll
stair_model/stair_model1.obj
stair_model/stair_model1.mtl
labrecorder/LabRecorder.exe
labrecorder/LabRecorderCLI.exe
labrecorder/LabRecorder.cfg
labrecorder/LICENSE
labrecorder/lsl*.dll
labrecorder/platforms/qwindows.dll
```

The regular zip is assembled from this directory. The native single-file
launcher embeds the same directory as its payload; Enigma Virtual Box remains
available when an `.evb` project and console are supplied.

To package a deployed GUI manually:

```powershell
.\packaging\windows\package_gui_single_exe.ps1 `
  -DeployDir .\package `
  -OutputExe .\vicon-lsl-bridge-gui-portable.exe `
  -LauncherExe .\build\Release\vicon-lsl-bridge-portable-launcher.exe `
  -LabRecorderDeployDir C:\path\recorder-deploy `
  -StairModelDir .\assets\stair_model
```

The script validates the bridge executable, Qt platform plugin, LSL runtime,
stair model, and the complete isolated LabRecorder deployment.
The CMake portable target accepts the same inputs through
`VICON_LSL_LABRECORDER_DEPLOY_DIR`; the tracked stair model is included
automatically.

For Enigma packaging, add `-Mode Enigma -ProjectFile path\bridge.evb` (and
optionally `-EnigmaConsole path\enigmavbconsole.exe`).
