# Windows GUI Single-Exe Packaging

The normal Windows release remains the deployed `package/` directory and zip produced with `windeployqt`.

For an unattended single portable GUI executable, package that deployed directory:

```powershell
.\packaging\windows\package_gui_single_exe.ps1 `
  -DeployDir .\package `
  -OutputExe .\vicon-lsl-bridge-gui-portable.exe `
  -LauncherExe .\build\Release\vicon-lsl-bridge-portable-launcher.exe
```

`Auto` mode uses Enigma Virtual Box when both its console executable and an `.evb` project are supplied. Otherwise it uses the project’s native portable launcher:

```powershell
.\packaging\windows\package_gui_single_exe.ps1 `
  -DeployDir .\package `
  -OutputExe .\vicon-lsl-bridge-gui-portable.exe `
  -LauncherExe .\build\Release\vicon-lsl-bridge-portable-launcher.exe
```

The native launcher embeds the deployed directory as a zip payload, extracts it to a unique temporary directory, waits for the GUI to exit, and then removes the extracted files.
Passing `--smoke-test` through the portable executable starts the real Qt GUI, performs a
local LSL outlet/resolver check, and exits with a status code for clean-machine CI validation.

To force Enigma:

```powershell
.\packaging\windows\package_gui_single_exe.ps1 `
  -DeployDir .\package `
  -OutputExe .\vicon-lsl-bridge-gui-portable.exe `
  -Mode Enigma `
  -ProjectFile .\package\vicon-lsl-bridge-gui.evb
```

The CMake target accepts the same Enigma route:

```powershell
cmake -S . -B build `
  -DVICON_LSL_ENIGMA_PROJECT=C:\path\vicon-lsl-bridge-gui.evb `
  -DVICON_LSL_ENIGMA_CONSOLE="C:\Program Files\Enigma Virtual Box\enigmavbconsole.exe"
cmake --build build --config Release --target vicon-lsl-bridge-gui-portable
```

The deployment directory must already contain `windeployqt` output, `liblsl.dll`, and any redistributable runtime DLLs.
The packaging script validates `platforms/qwindows.dll` and a matching `lsl*.dll`/`liblsl*.dll`
before creating the executable. The vendored Vicon SDK currently links statically; do not add
Vicon runtime DLLs to release artifacts unless their redistribution terms permit it.

The Windows CI smoke test runs the packaged GUI on a clean runner, verifies Qt startup, checks
that a failed Vicon connection returns cleanly, and resolves a temporary local LSL stream. A
release candidate still needs one manual connection test on the target Vicon network.
