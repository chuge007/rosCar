[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$toolDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$compilerCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\MSBuild\15.0\Bin\Roslyn\csc.exe',
    'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe',
    'C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe'
)
$compiler = $compilerCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($compiler)) {
    throw 'C# compiler not found. Install .NET Framework 4.x or Visual Studio.'
}

$outputDirectory = Join-Path $toolDirectory 'bin'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$outputFile = Join-Path $outputDirectory 'MwdMotorDetector.exe'
$sourceFile = Join-Path $toolDirectory 'Program.cs'

& $compiler /nologo /target:winexe /optimize+ /platform:anycpu `
    /reference:System.dll `
    /reference:System.Core.dll `
    /reference:System.Drawing.dll `
    /reference:System.Management.dll `
    /reference:System.Windows.Forms.dll `
    /out:$outputFile `
    $sourceFile
if ($LASTEXITCODE -ne 0) {
    throw "Compiler exited with code $LASTEXITCODE."
}

Write-Output $outputFile
