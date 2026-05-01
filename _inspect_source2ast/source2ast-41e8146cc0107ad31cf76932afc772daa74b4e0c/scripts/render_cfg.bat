@echo off
REM Script to render all DOT files to PNG images using Graphviz
REM Assumes dot.exe is in PATH

setlocal enabledelayedexpansion

set DOT_DIR=output
set IMG_DIR=output

REM Check if dot.exe is available
where dot.exe >nul 2>&1
if errorlevel 1 (
    echo Error: Graphviz (dot.exe) not found in PATH
    echo Please install Graphviz and ensure dot.exe is in your PATH
    exit /b 1
)

REM Create image directory if it doesn't exist
if not exist "%IMG_DIR%" mkdir "%IMG_DIR%"

REM Count DOT files
set COUNT=0
for %%f in ("%DOT_DIR%\*.dot") do (
    set /a COUNT+=1
)

if %COUNT% equ 0 (
    echo Error: No DOT files found in %DOT_DIR%
    exit /b 1
)

echo Found %COUNT% DOT file(s) to render...

REM Process each DOT file
set RENDERED=0
set FAILED=0

for %%f in ("%DOT_DIR%\*.dot") do (
    set "DOT_FILE=%%f"
    set "BASE_NAME=%%~nf"
    set "IMG_FILE=%IMG_DIR%\!BASE_NAME!.png"
    
    echo Rendering !DOT_FILE! to !IMG_FILE!...
    dot.exe -Tpng -Gdpi=120 "!DOT_FILE!" -o "!IMG_FILE!"
    
    if errorlevel 1 (
        echo Error: Failed to render !DOT_FILE!
        set /a FAILED+=1
    ) else (
        set /a RENDERED+=1
    )
)

echo.
echo Summary:
echo   Rendered: %RENDERED% image(s)
echo   Failed: %FAILED% file(s)

if %FAILED% gtr 0 (
    exit /b 1
)

exit /b 0

