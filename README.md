# FSEQ Model Viewer

Personal tool, not part of xLights. Pick a show folder, an `.fseq` and a model,
and play that model's channels in a preview window.

The show folder is loaded with the xLights core (`HeadlessRenderContext`), so
model types, start channels (`!controller:`, `>model:`, ...), string types and
node positions all match xLights.

- **Layout view**: node positions as in the Layout tab (Z dropped).
- **Buffer view**: the model's default render buffer grid.
- Model groups are not listed.
- Show folder, fseq, model and view are remembered between runs.

## Build (Windows)

1. Build the core library in the xLights repo (needed again whenever core headers change):

   ```
   cmake --build build-tests-full --config Release --target xlights_core
   ```

   First time only: `cmake -S . -B build-tests-full -G "Visual Studio 17 2022" -DXL_BUILD_TESTS=ON -DXL_TEST_FULL_CORE=ON`

2. Build the viewer (VS 18 CMake):

   ```
   cmake -S . -B build -G "Visual Studio 18 2026" -A x64
   cmake --build build --config Release
   build\Release\FseqModelViewer.exe
   ```

Paths default to `H:/XlightsSourceDir/xlights` and `H:/XlightsSourceDir/wxWidgets`;
override with `-DXLIGHTS_SRC=...`, `-DXLIGHTS_CORE_LIB=...`, `-DwxWidgets_ROOT_DIR=...`.
