# Windows GUI packaging

Every Windows artifact uses one directory layout:

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
licenses/              # verbatim upstream license texts (including Qt LICENSES)
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

Native portable packaging writes the SHA-256 of `payload.zip` into a fixed
launcher image slot before appending the payload.  The launcher verifies that
digest before extraction, so payload corruption is rejected before extraction.

The portable executable accepts `--extract <directory>`. It verifies the
embedded payload digest and expands the complete application tree into a new,
empty directory; the destination must not already exist and may not be a
reparse point or junction. Users can replace LGPL-covered Qt DLLs in that
directory (including the nested `labrecorder` Qt DLLs) and run
`vicon-lsl-bridge-gui.exe` themselves. The `--test` switch remains available
for the portable self-test.

Windows release artifacts are intentionally unsigned. Windows may therefore
display an unknown-publisher or reputation warning. Verify downloaded files
against the release's `SHA256SUMS.txt` before running them.

The directory ZIP and portable executable are both retained.  Linux releases
continue to publish the existing tarball.  Release metadata also includes a
`SHA256SUMS.txt` inventory and checks that the `vN.N.N` tag matches the CMake
project version. `THIRD_PARTY_NOTICES.txt`, `LICENSE-INVENTORY.txt`, the Vicon
SDK license, and the complete `licenses/` bundle are mandatory in Windows
layouts; packaging fails when any required upstream text cannot be located. No
installer format is produced.

CI obtains the Qt 6.8.3 `qtbase` and `qtsvg` `LICENSES` directories from the
official source archives and verifies their published SHA-256 hashes before
packaging them. Binary-only Qt installations that omit these texts must supply
`VICON_LSL_QT_LICENSE_ROOT` when building the local portable target.

For Enigma packaging, add `-Mode Enigma -ProjectFile path\bridge.evb` (and
optionally `-EnigmaConsole path\enigmavbconsole.exe`).

The `msvcp140*.dll` and `vcruntime140*.dll` files are Microsoft Visual C++
Redistributable runtime binaries copied from the x64 VC143 redist installed by
Visual Studio. They remain subject to Microsoft's Visual C++ Redistributable
license terms; this project does not reproduce that EULA. See Microsoft's
[latest supported VC++ Redistributable documentation](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)
and [Visual Studio license terms](https://visualstudio.microsoft.com/license-terms/).
