# Package the Windows app

The Windows ZIP and the portable app contain the same files. The ZIP shows the files directly. The portable `.exe` stores a ZIP inside one launcher file and extracts it when you run it.

## Package contents

Every Windows package uses this layout:

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
licenses/              # exact copies of upstream license files, including Qt LICENSES
```

Packaging stops with an error if a required program, model file, runtime library, or license file is missing.

## Build the portable app by hand

From `vicon-lsl-bridge`, run:

```powershell
.\packaging\windows\package_gui_single_exe.ps1 `
  -DeployDir .\package `
  -OutputExe .\vicon-lsl-bridge-gui-portable.exe `
  -LauncherExe .\build\Release\vicon-lsl-bridge-portable-launcher.exe `
  -LabRecorderDeployDir C:\path\recorder-deploy `
  -StairModelDir .\assets\stair_model
```

The matching CMake target uses `VICON_LSL_LABRECORDER_DEPLOY_DIR` for the LabRecorder folder. It adds the tracked stair model on its own.

Enigma Virtual Box is still supported when you provide its project and command-line program:

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

## Check and extract the portable app

The launcher stores the SHA-256 checksum of its embedded ZIP. It checks that value before extracting anything. A changed or damaged embedded ZIP is rejected.

Run an internal package check with:

```powershell
.\vicon-lsl-bridge-gui-portable.exe --test
```

Extract all files with:

```powershell
.\vicon-lsl-bridge-gui-portable.exe --extract C:\new\empty\folder
```

The destination must not exist yet. It also cannot be a Windows reparse point or junction, which can redirect writes to another folder.

After extraction, you may replace the Qt libraries covered by the LGPL. This includes the Qt libraries under `labrecorder`. Start `vicon-lsl-bridge-gui.exe` from the extracted folder when you are done.

## Release files

A Windows release contains:

- A normal ZIP.
- A portable GUI `.exe`.
- `SHA256SUMS.txt`, which lists the checksum for each release file.

Linux releases keep using a `.tar.gz` archive. This project does not create a Windows installer.

Windows release files are not signed. Windows may show an unknown-publisher or reputation warning. Before running a download, compare its checksum with the release copy of `SHA256SUMS.txt`.

The release tag must use `vN.N.N`, and its version must match the CMake project version.

## License files

Every package must include:

- `THIRD_PARTY_NOTICES.txt`
- `LICENSE-INVENTORY.txt`
- `VICON-DATASTREAM-SDK-LICENSE.txt`
- The full `licenses/` folder

The hosted build downloads the Qt 6.8.3 `qtbase` and `qtsvg` license folders from the official source archives. It checks their published SHA-256 values before adding them.

Some Qt binary packages do not include these license files. For a local build, set `VICON_LSL_QT_LICENSE_ROOT` to a folder that contains them.

The `msvcp140*.dll` and `vcruntime140*.dll` files come from the x64 VC143 Visual C++ Redistributable installed with Visual Studio. Microsoft's license terms still apply. See the [supported Visual C++ Redistributable downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) and [Visual Studio license terms](https://visualstudio.microsoft.com/license-terms/).
