[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('status', 'analyze', 'query', 'context', 'serve')]
    [string]$Command = 'status',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Arguments = @()
)

$ErrorActionPreference = 'Stop'
$PinnedVersion = '1.6.9'
$RepositoryName = 'rosCar'
$allowBuild = @('--allow-build=@ladybugdb/core', '--allow-build=gitnexus', '--allow-build=tree-sitter')

if ($Command -in @('query', 'context') -and
    -not ($Arguments | Where-Object { $_ -in @('-r', '--repo') })) {
    $Arguments += @('--repo', $RepositoryName)
}

function Invoke-Runner([string]$program, [string[]]$runnerArgs) {
    Write-Host ("GitNexus: " + $program + ' ' + ($runnerArgs -join ' ')) -ForegroundColor DarkGray
    & $program @runnerArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$global = Get-Command gitnexus -ErrorAction SilentlyContinue
if ($global) {
    $version = (& $global.Source --version 2>$null | Select-Object -First 1).ToString().Trim()
    if ($version -eq $PinnedVersion) {
        Invoke-Runner $global.Source (@($Command) + $Arguments)
        exit 0
    }
    Write-Host "Ignoring global GitNexus $version; this repository pins $PinnedVersion for Node 22.15 compatibility." -ForegroundColor Yellow
}

$pnpm = Get-Command pnpm -ErrorAction SilentlyContinue
if ($pnpm) {
    Invoke-Runner $pnpm.Source ($allowBuild + @('dlx', "gitnexus@$PinnedVersion", $Command) + $Arguments)
    exit 0
}

$npx = Get-Command npx -ErrorAction SilentlyContinue
if ($npx) {
    Invoke-Runner $npx.Source (@('--yes', "gitnexus@$PinnedVersion", $Command) + $Arguments)
    exit 0
}

throw "GitNexus is unavailable. Install Node.js/npm and pnpm, or install gitnexus@$PinnedVersion globally."
