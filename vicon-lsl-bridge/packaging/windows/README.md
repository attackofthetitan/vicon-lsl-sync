# Package the Windows app

The Windows ZIP and the portable app hold the same files. The portable `.exe` is a
launcher with the ZIP inside it, which it unpacks when you run it.

## What's in the package

```text
vicon-lsl-bridge-gui.exe
vicon-lsl-bridge.exe
lsl*.dll
msvcp140.dll
vcruntime140.dll
vcruntime140_1.dll
platforms/qwindows.dll
stair_model/stair_model1.obj
stair_model/stair_model1.mtl
labrecorder/LabRecorder.exe
labrecorder/LabRecorderCLI.exe
labrecorder/LabRecorder.cfg
labrecorder/LICENSE
labrecorder/lsl*.dll
labrecorder/platforms/qwindows.dll
THIRD_PARTY_NOTICES.txt
VICON-DATASTREAM-SDK-LICENSE.txt
LICENSE-INVENTORY.txt
licenses/              # exact copies of each project's license files, including Qt LICENSES
```

Packaging stops with an error if any program, model file, runtime library, or
license file is missing.

## Build the portable app yourself

From `vicon-lsl-bridge`:

```powershell
.\packaging\windows\package_gui_single_exe.ps1 `
  -DeployDir .\package `
  -OutputExe .\vicon-lsl-bridge-gui-portable.exe `
  -LauncherExe .\build\Release\vicon-lsl-bridge-portable-launcher.exe `
  -LabRecorderDeployDir C:\path\recorder-deploy `
  -StairModelDir .\assets\stair_model
```

The matching CMake target takes the LabRecorder folder from
`VICON_LSL_LABRECORDER_DEPLOY_DIR` and adds the stair model itself.

You can still use Enigma Virtual Box if you give it its project file and console
program:

```powershell
.\packaging\windows\package_gui_single_exe.ps1 `
  -Mode Enigma `
  -ProjectFile path\bridge.evb `
  -EnigmaConsole path\enigmavbconsole.exe `
  -DeployDir .\package `
  -OutputExe .\vicon-lsl-bridge-gui-portable.exe `
  -LabRecorderDeployDir C:\path\recorder-deploy `
  -StairModelDir .\assets\stair_model
```

## Check and unpack the portable app

The launcher stores a SHA-256 checksum of its ZIP and checks it before unpacking.
If the ZIP was changed or damaged, it won't run.

```powershell
.\vicon-lsl-bridge-gui-portable.exe --test                       # check it
.\vicon-lsl-bridge-gui-portable.exe --extract C:\new\empty\folder  # unpack it
```

The folder must not exist yet, and can't be a junction or other link. Once
unpacked, you can swap in your own copies of the LGPL Qt libraries, including the
ones under `labrecorder`, and start `vicon-lsl-bridge-gui.exe` from there.

## Release files

A Windows release has a ZIP, a portable `.exe`, and `SHA256SUMS.txt` with a
checksum for each file. Linux gets a `.tar.gz`. There's no Windows installer.

Windows files aren't signed, so Windows may warn about an unknown publisher. Check
a download against `SHA256SUMS.txt` before running it.

The release tag must look like `vN.N.N` and match the version in CMake.

## License files

Every package needs `THIRD_PARTY_NOTICES.txt`, `LICENSE-INVENTORY.txt`,
`VICON-DATASTREAM-SDK-LICENSE.txt`, and the whole `licenses/` folder.

The release build downloads the Qt 6.8.3 `qtbase` and `qtsvg` license folders from
Qt's official source archives and checks their published SHA-256 values. Some Qt
downloads don't include them, so for a local build, set
`VICON_LSL_QT_LICENSE_ROOT` to a folder that has them.

The `msvcp140*.dll` and `vcruntime140*.dll` files come from the x64 VC143 Visual
C++ Redistributable that installs with Visual Studio. Microsoft's terms still
apply: see the [supported Visual C++ Redistributable
downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)
and [Visual Studio license terms](https://visualstudio.microsoft.com/license-terms/).
