# Script to render all DOT files to PNG images using Graphviz
# Cross-platform: works on Windows and Unix-like systems

$DOT_DIR = "output"
$IMG_DIR = "output"

# Determine the correct dot command name based on platform
$dotCmd = "dot"
if ($IsWindows -or $env:OS -eq "Windows_NT") {
    $dotCmd = "dot.exe"
}

# Check if dot is available
$dotPath = Get-Command $dotCmd -ErrorAction SilentlyContinue
if (-not $dotPath) {
    Write-Host "Error: Graphviz ($dotCmd) not found in PATH" -ForegroundColor Red
    Write-Host "Please install Graphviz and ensure $dotCmd is in your PATH" -ForegroundColor Red
    exit 1
}

# Create image directory if it doesn't exist
if (-not (Test-Path $IMG_DIR)) {
    New-Item -ItemType Directory -Path $IMG_DIR | Out-Null
}

# Get all DOT files
$dotFiles = Get-ChildItem -Path $DOT_DIR -Filter "*.dot" -ErrorAction SilentlyContinue

if ($dotFiles.Count -eq 0) {
    Write-Host "Error: No DOT files found in $DOT_DIR" -ForegroundColor Red
    exit 1
}

Write-Host "Found $($dotFiles.Count) DOT file(s) to render..." -ForegroundColor Green

$rendered = 0
$failed = 0

# Process each DOT file
foreach ($dotFile in $dotFiles) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($dotFile.Name)
    $imgFile = Join-Path $IMG_DIR "$baseName.png"
    
    Write-Host "Rendering $($dotFile.Name) to $imgFile..." -ForegroundColor Cyan
    
    $process = Start-Process -FilePath $dotCmd -ArgumentList "-Tpng", "-Gdpi=120", "`"$($dotFile.FullName)`"", "-o", "`"$imgFile`"" -Wait -PassThru -NoNewWindow
    
    if ($process.ExitCode -ne 0) {
        Write-Host "Error: Failed to render $($dotFile.Name)" -ForegroundColor Red
        $failed++
    } else {
        $rendered++
    }
}

Write-Host ""
Write-Host "Summary:" -ForegroundColor Green
Write-Host "  Rendered: $rendered image(s)" -ForegroundColor Green
Write-Host "  Failed: $failed file(s)" -ForegroundColor $(if ($failed -gt 0) { "Red" } else { "Green" })

if ($failed -gt 0) {
    exit 1
}

exit 0

