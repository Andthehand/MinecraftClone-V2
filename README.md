# Minecraft Clone

## Tracy profiling

From an x64 Visual Studio developer shell with Vulkan SDK 1.4.363.0 installed:

```powershell
cmake --preset windows-profile
cmake --build --preset windows-profile
Set-Location out/build/windows-profile
./MinecraftClone.exe
```

Run the game with `out/build/windows-profile` as the working directory so its
copied shaders can be found. Open the **Tracy 0.15.0** profiler and connect to
`MinecraftClone`. The client collects on demand, so the game can run without
an attached profiler. To capture startup, start the profiler before the game;
use `-DTRACY_ON_DEMAND=OFF` when configuring if you need buffered startup events.

The capture includes a named main thread, frame markers, CPU zones for startup,
events, camera updates, terrain generation, octree expansion, chunk meshing and
uploads, visibility, shadow setup, and render graph construction. RealRHI's CPU
zones share the same client, including shader compilation, graph execution,
command recording, submission, and waits. Plots track loaded, generated, visible,
and shadow-visible chunk counts. These are CPU timings; GPU timestamps are not
collected.

Tracy is fetched automatically only for profiling builds. The regular
`windows-debug` and `windows-release` presets leave profiling disabled by default.
For another configuration, set `MINECRAFT_ENABLE_TRACY=ON` to enable profiling in
both the game and RealRHI, or `OFF` to compile out the instrumentation without
fetching or linking Tracy. `TRACY_ON_DEMAND` may be overridden independently.

### Viewing assembly

Machine code is sent when Tracy requests a resolved function's instructions.
Opening a zone's source location can show only source when Tracy has no unique
matching captured symbol; this does not mean code transfer is disabled.

For a fresh capture on Windows, run the game as administrator to enable CPU
sampling. Keep the matching `MinecraftClone.pdb` beside the executable. In Tracy,
open **Statistics**, select **Sampling**, open a function from the game, and choose
**Assembly** or **Both** in its symbol view. A plain source view has no assembly
selector. Code from a DLL that has already unloaded can remain unavailable.

### Visual Studio access-violation stops

The profiling preset explicitly sets `TRACY_NO_CODE_TRANSFER=OFF`, enabling
source-code and machine-code/disassembly transfer.
Tracy can request machine code from a DLL that has already unloaded. Its default
Windows copy catches the resulting access violation, but Visual Studio can break
before that handler runs.

After changing this setting, stop the current debugging session, configure and
build `windows-profile` again, then restart the game.
