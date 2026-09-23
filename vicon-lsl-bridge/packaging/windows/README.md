# Package the Windows app

The Windows ZIP and the portable app hold the same files. The ZIP shows them as normal files. The portable `.exe` is a single launcher with a ZIP tucked inside, which it unpacks when you run it.

## What is in the package

Every Windows package looks like this:

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

Packaging stops with an error if any required program, model file, runtime library, or license file is missing.

## Build the portable app yourself

From `vicon-lsl-bridge`, run:

```powershell
.\packaging\windows\package_gui_single_exe.ps1 `
  -DeployDir .\package `
  -OutputExe .\vicon-lsl-bridge-gui-portable.exe `
  -LauncherExe .\build\Release\vicon-lsl-bridge-portable-launcher.exe `
  -LabRecorderDeployDir C:\path\recorder-deploy `
  -StairModelDir .\assets\stair_model
```

The matching CMake target takes the LabRecorder folder from `VICON_LSL_LABRECORDER_DEPLOY_DIR`, and adds the stair model by itself.

You can still use Enigma Virtual Box if you give it its project file and command-line program:

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

The launcher stores a SHA-256 checksum of the ZIP inside it, and checks it before unpacking anything. If the ZIP was changed or damaged, the launcher refuses to run.

To check the package, run:

```powershell
.\vicon-lsl-bridge-gui-portable.exe --test
```

To unpack every file, run:

```powershell
.\vicon-lsl-bridge-gui-portable.exe --extract C:\new\empty\folder
```

The folder must not exist yet. It also cannot be a Windows junction or other link that could send the files somewhere else.

Once unpacked, you are free to swap in your own copies of the Qt libraries covered by the LGPL, including the ones under `labrecorder`. Then start `vicon-lsl-bridge-gui.exe` from the unpacked folder.

## Release files

A Windows release has:

- A normal ZIP.
- A portable app `.exe`.
- `SHA256SUMS.txt`, which lists a checksum for each release file.

Linux releases are still a `.tar.gz` archive. This project does not make a Windows installer.

Windows release files are not signed, so Windows may warn that the publisher is unknown. Before running a download, check its checksum against the release's `SHA256SUMS.txt`.

The release tag must look like `vN.N.N`, and its version must match the version in CMake.

## License files

Every package must have:

- `THIRD_PARTY_NOTICES.txt`
- `LICENSE-INVENTORY.txt`
- `VICON-DATASTREAM-SDK-LICENSE.txt`
- The whole `licenses/` folder

The release build downloads the Qt 6.8.3 `qtbase` and `qtsvg` license folders from Qt's official source archives, and checks their published SHA-256 values before adding them.

Some Qt downloads do not include these license files. For a local build, set `VICON_LSL_QT_LICENSE_ROOT` to a folder that has them.

The `msvcp140*.dll` and `vcruntime140*.dll` files come from the x64 VC143 Visual C++ Redistributable that installs with Visual Studio. Microsoft's license terms still apply. See the [supported Visual C++ Redistributable downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) and [Visual Studio license terms](https://visualstudio.microsoft.com/license-terms/).
