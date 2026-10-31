<#
.SYNOPSIS
    CI/CD runner script for misra_checker.exe (designed for GitLab CI / .gitlab-ci.yml).

.DESCRIPTION
    Scans a target C++ directory with misra_checker.exe while supporting file,
    wildcard, and directory exclusions at both translation-unit discovery time
    and violation report filtering time (so excluded headers such as `test.h`
    or `*.h` included by `.cpp` files are also excluded from the final report).

.PARAMETER ScanDir
    Full (or relative) path to the directory to scan.

.PARAMETER Exclude
    One or more exclusion patterns (array of strings, or comma/semicolon-separated string).
    Supported formats:
      - Specific filename:  "test.h", "legacy.cpp"
      - Wildcard pattern:   "*.h", "test_*.cpp", "*\mock\*"
      - Directory/subpath:  "\src\test\exclude", "src/test/exclude", "C:\repo\src\test\exclude"

.PARAMETER CheckerExe
    Path to misra_checker.exe. Defaults to build\Release\Release\misra_checker.exe.

.PARAMETER OutputFile
    Path to the output report file (default: misra_output.txt).

.PARAMETER CompileCommandsDir
    Optional path to a directory containing compile_commands.json (passed via -p).

.PARAMETER ExtraCompilerArgs
    Additional compiler flags passed after `--` when no compile_commands.json is used
    (default: -std=c++17).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\run_misra_ci.ps1 -ScanDir "C:\repo\src" -Exclude "test.h","*.hpp","\src\test\exclude"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$ScanDir,

    [Parameter(Mandatory = $false, Position = 1)]
    [string[]]$Exclude = @(),

    [Parameter(Mandatory = $false)]
    [string]$CheckerExe = "",

    [Parameter(Mandatory = $false)]
    [string]$OutputFile = "misra_output.txt",

    [Parameter(Mandatory = $false)]
    [string]$CompileCommandsDir = "",

    [Parameter(Mandatory = $false)]
    [string[]]$ExtraCompilerArgs = @("-std=c++17")
)

$ErrorActionPreference = "Stop"

# ── 1. Resolve paths ──────────────────────────────────────────────────────────
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Test-Path -LiteralPath $ScanDir -PathType Container)) {
    Write-Error "ScanDir does not exist or is not a directory: '$ScanDir'"
    exit 2
}
$ScanDirFull = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $ScanDir).Path).TrimEnd('\', '/')

if ([string]::IsNullOrWhiteSpace($CheckerExe)) {
    $Candidates = @(
        (Join-Path $ScriptRoot "build\Release\Release\misra_checker.exe"),
        (Join-Path $ScriptRoot "build\Release\misra_checker.exe"),
        (Join-Path $ScriptRoot "misra_checker.exe")
    )
    foreach ($c in $Candidates) {
        if (Test-Path -LiteralPath $c -PathType Leaf) {
            $CheckerExe = (Resolve-Path -LiteralPath $c).Path
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($CheckerExe) -or -not (Test-Path -LiteralPath $CheckerExe -PathType Leaf)) {
    Write-Error "Could not locate misra_checker.exe. Build the project first or pass -CheckerExe."
    exit 2
}

# Flatten comma/semicolon-separated exclusion strings so GitLab CI variables work seamlessly.
$NormalizedExcludes = New-Object System.Collections.Generic.List[string]
foreach ($item in $Exclude) {
    if ([string]::IsNullOrWhiteSpace($item)) { continue }
    foreach ($part in ($item -split '[,;]')) {
        $trimmed = $part.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $NormalizedExcludes.Add(($trimmed -replace '/', '\'))
        }
    }
}

# ── 2. Helper: Check whether a file path matches any exclusion rule ───────────
function Test-FileExcluded {
    param(
        [string]$FilePath,
        [string]$RootDir,
        [System.Collections.Generic.List[string]]$Patterns
    )
    if ($Patterns.Count -eq 0) { return $false }

    # Resolve relative paths against current location or RootDir
    $FullFile = $FilePath
    if (-not [System.IO.Path]::IsPathRooted($FullFile)) {
        $CandidateFromPwd = Join-Path (Get-Location).Path $FullFile
        $CandidateFromRoot = Join-Path $RootDir $FullFile
        if (Test-Path -LiteralPath $CandidateFromPwd) {
            $FullFile = [System.IO.Path]::GetFullPath($CandidateFromPwd)
        } elseif (Test-Path -LiteralPath $CandidateFromRoot) {
            $FullFile = [System.IO.Path]::GetFullPath($CandidateFromRoot)
        } else {
            $FullFile = [System.IO.Path]::GetFullPath($CandidateFromPwd)
        }
    } else {
        $FullFile = [System.IO.Path]::GetFullPath($FullFile)
    }

    $FullNorm = $FullFile -replace '/', '\'
    $FileName = [System.IO.Path]::GetFileName($FullNorm)

    # Relative path from $RootDir (with and without leading backslash)
    $RelNoLead = $FullNorm
    if ($FullNorm.StartsWith($RootDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        $RelNoLead = $FullNorm.Substring($RootDir.Length).TrimStart('\')
    }
    $RelWithLead = "\" + $RelNoLead

    foreach ($pat in $Patterns) {
        $hasWildcard = ($pat.IndexOf('*') -ge 0) -or ($pat.IndexOf('?') -ge 0)
        $hasSlash    = $pat.IndexOf('\') -ge 0

        if ($hasWildcard) {
            if ($hasSlash) {
                if (($RelWithLead -like $pat) -or ($RelNoLead -like $pat) -or ($FullNorm -like $pat) -or ($RelWithLead -like "*$pat*")) {
                    return $true
                }
            } else {
                # Filename wildcard, e.g. "*.h" or "test_*.cpp"
                if ($FileName -like $pat) {
                    return $true
                }
            }
        } elseif (-not $hasSlash) {
            # Exact filename or single directory segment, e.g. "test.h"
            if ($FileName -ieq $pat) {
                return $true
            }
            if ($RelWithLead -imatch ("(^|\\)" + [regex]::Escape($pat) + "(\\|$)")) {
                return $true
            }
        } else {
            # Directory path or relative/full path, e.g. "\src\test\exclude"
            $cleanPat = $pat.Trim('\')
            if ([string]::IsNullOrWhiteSpace($cleanPat)) { continue }

            # Match against full path if pattern is rooted
            if ([System.IO.Path]::IsPathRooted($pat) -and -not $pat.StartsWith("\")) {
                $patFull = [System.IO.Path]::GetFullPath($pat).TrimEnd('\')
                if (($FullNorm -ieq $patFull) -or $FullNorm.StartsWith("$patFull\", [System.StringComparison]::OrdinalIgnoreCase)) {
                    return $true
                }
            }

            # Match against relative path prefix or subpath segment
            if (($RelNoLead -ieq $cleanPat) -or
                $RelNoLead.StartsWith("$cleanPat\", [System.StringComparison]::OrdinalIgnoreCase) -or
                ($RelWithLead -imatch ("\\" + [regex]::Escape($cleanPat) + "(\\|$)")) -or
                ($FullNorm -imatch ("\\" + [regex]::Escape($cleanPat) + "(\\|`$)"))) {
                return $true
            }
        }
    }

    return $false
}

# ── 3. Discover C/C++ translation units & filter excluded files ───────────────
$SourceExtensions = @(".cpp", ".c", ".cc", ".cxx")
$AllSources = Get-ChildItem -LiteralPath $ScanDirFull -Recurse -File |
    Where-Object { $SourceExtensions -contains $_.Extension.ToLowerInvariant() }

$IncludedSources = New-Object System.Collections.Generic.List[string]
foreach ($src in $AllSources) {
    if (-not (Test-FileExcluded -FilePath $src.FullName -RootDir $ScanDirFull -Patterns $NormalizedExcludes)) {
        $IncludedSources.Add($src.FullName)
    }
}

Write-Host "================================================================"
Write-Host "  MISRA C++:2008 CI/CD Runner"
Write-Host "  Scan Directory : $ScanDirFull"
Write-Host "  Exclusions     : $(if ($NormalizedExcludes.Count -gt 0) { $NormalizedExcludes -join ', ' } else { '(none)' })"
Write-Host "  Source Files   : $($IncludedSources.Count) of $($AllSources.Count) selected"
Write-Host "================================================================"

if ($IncludedSources.Count -eq 0) {
    Write-Host "No C/C++ source files remaining to analyze after exclusions."
    exit 0
}

# ── 4. Build misra_checker.exe argument list ──────────────────────────────────
$TempReport = [System.IO.Path]::GetTempFileName()
$CmdArgs = New-Object System.Collections.Generic.List[string]
foreach ($s in $IncludedSources) {
    $CmdArgs.Add($s)
}
$CmdArgs.Add("--project-root=$ScanDirFull")
$CmdArgs.Add("--output=$TempReport")

if (-not [string]::IsNullOrWhiteSpace($CompileCommandsDir)) {
    $CmdArgs.Add("-p")
    $CmdArgs.Add($CompileCommandsDir)
} else {
    $CmdArgs.Add("--")
    foreach ($arg in $ExtraCompilerArgs) {
        $CmdArgs.Add($arg)
    }
    $CmdArgs.Add("-I$ScanDirFull")
}

# ── 5. Execute misra_checker.exe ──────────────────────────────────────────────
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $CheckerExe @CmdArgs > $null 2>&1
$ErrorActionPreference = $prevEAP

if (-not (Test-Path -LiteralPath $TempReport -PathType Leaf)) {
    Write-Error "misra_checker.exe did not produce a report file."
    exit 2
}

# ── 6. Filter excluded files (e.g. excluded .h headers) from the report ──────
$ReportLines = Get-Content -LiteralPath $TempReport
Remove-Item -LiteralPath $TempReport -Force -ErrorAction SilentlyContinue

if ($NormalizedExcludes.Count -eq 0) {
    # No exclusions: write and display report directly
    $ReportLines | Set-Content -Path $OutputFile -Encoding UTF8
    $ReportLines | ForEach-Object { Write-Host $_ }
    $TotalMatch = $ReportLines | Select-String -Pattern '^\s*Total:\s*(\d+)\s+violation'
    $TotalCount = if ($TotalMatch) { [int]$TotalMatch.Matches[0].Groups[1].Value } else { 0 }
    if ($TotalCount -gt 0) { exit 1 } else { exit 0 }
}

# Parse violations and rule summary from the generated report
$TagLine = ""
$RegisteredRules = New-Object System.Collections.Generic.List[string]
$KeptViolations = New-Object System.Collections.Generic.List[PSCustomObject]
$RuleCounts = @{}

$Section = "header"
for ($i = 0; $i -lt $ReportLines.Count; $i++) {
    $line = $ReportLines[$i]
    if ($line -match '^\s*Tag:\s*(.+)$') {
        $TagLine = $line
    } elseif ($line -match '^Violations \(\d+\):') {
        $Section = "violations"
        $i++ # skip dashed separator
        continue
    } elseif ($line -match '^Summary by rule:') {
        $Section = "summary"
        continue
    }

    if ($Section -eq "violations") {
        if ($line -match '^-{10,}$') {
            $Section = "after_violations"
            continue
        }
        if ($line -match '^(.+):(\d+):(\d+)\s+\[([A-Z0-9\-]+)\]\s+(.+)$') {
            $vFile = $Matches[1]
            $vRule = $Matches[4]
            $srcLine = ""
            if (($i + 1 -lt $ReportLines.Count) -and ($ReportLines[$i + 1] -match '^\s*\|\s*(.*)$')) {
                $srcLine = $ReportLines[$i + 1]
                $i++
            }
            if (-not (Test-FileExcluded -FilePath $vFile -RootDir $ScanDirFull -Patterns $NormalizedExcludes)) {
                $KeptViolations.Add([PSCustomObject]@{
                    HeaderLine = $line
                    SourceLine = $srcLine
                    RuleId     = $vRule
                })
            }
        }
    } elseif ($Section -eq "summary") {
        if ($line -match '^\s*(MISRA-[0-9\-]+)\s+\d+\s+violation') {
            $rId = $Matches[1]
            $RegisteredRules.Add($rId)
            $RuleCounts[$rId] = 0
        }
    }
}

foreach ($v in $KeptViolations) {
    if ($RuleCounts.ContainsKey($v.RuleId)) {
        $RuleCounts[$v.RuleId]++
    } else {
        $RegisteredRules.Add($v.RuleId)
        $RuleCounts[$v.RuleId] = 1
    }
}

# Reconstruct the filtered report
$OutLines = New-Object System.Collections.Generic.List[string]
$OutLines.Add("================================================================")
$OutLines.Add("  MISRA C++:2008 Static Analysis Report")
if (-not [string]::IsNullOrWhiteSpace($TagLine)) {
    $OutLines.Add($TagLine)
}
$OutLines.Add("================================================================")
$OutLines.Add("")
$OutLines.Add("Scanned files ($($IncludedSources.Count)):")
foreach ($s in $IncludedSources) {
    $OutLines.Add("  $s")
}
$OutLines.Add("")

if ($KeptViolations.Count -eq 0) {
    $OutLines.Add("No violations found.")
    $OutLines.Add("")
} else {
    $OutLines.Add("Violations ($($KeptViolations.Count)):")
    $OutLines.Add(("-" * 72))
    foreach ($v in $KeptViolations) {
        $OutLines.Add($v.HeaderLine)
        if (-not [string]::IsNullOrWhiteSpace($v.SourceLine)) {
            $OutLines.Add($v.SourceLine)
        }
    }
    $OutLines.Add(("-" * 72))
    $OutLines.Add("")
}

$OutLines.Add("Summary by rule:")
foreach ($rId in $RegisteredRules) {
    $cnt = $RuleCounts[$rId]
    $plural = if ($cnt -eq 1) { "" } else { "s" }
    $OutLines.Add(("  {0,-20}{1} violation{2}" -f $rId, $cnt, $plural))
}
$totalPlural = if ($KeptViolations.Count -eq 1) { "" } else { "s" }
$OutLines.Add("")
$OutLines.Add("  Total: $($KeptViolations.Count) violation$totalPlural")
$OutLines.Add("================================================================")

$OutLines | Set-Content -Path $OutputFile -Encoding UTF8
$OutLines | ForEach-Object { Write-Host $_ }
Write-Host ""
Write-Host "Report written to: $OutputFile"

if ($KeptViolations.Count -gt 0) {
    exit 1
}
exit 0
