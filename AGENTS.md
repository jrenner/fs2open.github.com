# Agent Notes

## Windows Build Guidance

This checkout has an existing Ninja build directory at `F:\fso-github\build`.
It was configured with Visual Studio 2019/MSVC from `D:\vs_studio_2019`.

Use the Visual Studio developer environment before building. Without it, `cl.exe`
may run but fail to find standard headers such as `stdio.h` or `bitset`.

Recommended build command from PowerShell:

```powershell
$elapsed = Measure-Command {
  cmd /s /c '"D:\vs_studio_2019\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "D:\vs_studio_2019\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -C F:\fso-github\build'
}
Write-Host ("BUILD_SECONDS={0:N1}" -f $elapsed.TotalSeconds)
```

Notes:

- Do not assume `ninja` is on `PATH`; use the absolute Ninja path above or read
  `CMAKE_MAKE_PROGRAM` from `F:\fso-github\build\CMakeCache.txt`.
- Do not build target `fs2_open_25_1_0_x64_AVX2` by name; in this build that is
  the executable name, not an exposed Ninja target. Build the default target.
- If `cmake --build F:\fso-github\build --config Release` exits without useful
  diagnostics, run Ninja directly through `VsDevCmd.bat` with `-v`.
- The output executable is
  `F:\fso-github\build\bin\fs2_open_25_1_0_x64_AVX2.exe`.
- Recent incremental rebuilds of tactical-map changes took about 84-99 seconds
  on this machine, mostly due to final link/LTO time.
