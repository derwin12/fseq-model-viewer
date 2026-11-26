@echo off
rem Retest: open the viewer on a known show / fseq / model.
rem Edit these three lines to test something else. MODEL may be left empty.
set SHOW=E:\2023\ShowFolder3D
set FSEQ=%SHOW%\A Holly Jolly Christmas - Burl Ives.fseq
set MODEL=Matrix Seeds

set EXE=%~dp0build\Release\FseqModelViewer.exe

if not exist "%EXE%" (
    echo Viewer not built: %EXE%
    echo Build it with:  cmake --build "%~dp0build" --config Release
    pause
    exit /b 1
)
if not exist "%FSEQ%" (
    echo FSEQ not found: %FSEQ%
    pause
    exit /b 1
)

start "" "%EXE%" "%SHOW%" "%FSEQ%" "%MODEL%"
