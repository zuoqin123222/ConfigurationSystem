[CmdletBinding()]
param(
    [string[]]$Target = @("All"),
    [switch]$DryRun,
    [switch]$SkipTests,
    [switch]$AllowDirty,
    [switch]$CleanFailedStaging,
    [ValidateSet("debug", "shipping")]
    [string]$Profile = "shipping",
    [ValidateSet("estimate", "coverage", "shard", "exhaustive")]
    [string]$Mode = "coverage",
    [string]$Shard = "0/1",
    [string]$Publication = "sc01-v2",
    [switch]$IncludeRenders,
    [Alias("Input")]
    [string]$BakeInput = "contracts/fixtures/sc01.catalog.draft.v2.json",
    [Alias("Output")]
    [string]$BakeOutput = "staging/release-bake",
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$WebRoot = Join-Path $RepositoryRoot "source\clients\web"
$ServerRoot = Join-Path $RepositoryRoot "source\server"
$ProjectPath = Join-Path $RepositoryRoot "source\clients\ue\ConfigurationSystem.uproject"
$PackageRoot = Join-Path $RepositoryRoot "package"
$EmbeddedWebRoot = Join-Path $RepositoryRoot "source\clients\ue\Content\WebUI"
$WebDeployScript = Join-Path $WebRoot "scripts\deploy-embedded.mjs"
$BakeOwnershipSentinelName = ".configuration-system-bake-output"
$script:WebArtifact = $null
$script:PreparedReleaseItems = @()
$script:SourceWasDirty = $null
$script:SourceCommit = $null

function Resolve-RepositoryPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $Path))
}

function Test-PathWithin {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root
    )
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return $fullPath.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith(
            $fullRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase
        )
}

function Assert-SafeBakeDestination {
    param([Parameter(Mandatory = $true)][string]$Destination)
    $protectedRoots = @(
        (Join-Path $RepositoryRoot ".git"),
        (Join-Path $RepositoryRoot "source"),
        (Join-Path $RepositoryRoot "contracts"),
        (Join-Path $RepositoryRoot "tools"),
        (Join-Path $RepositoryRoot "docs"),
        (Join-Path $RepositoryRoot "harness")
    )
    $fullDestination = [IO.Path]::GetFullPath($Destination)
    if ($fullDestination.TrimEnd('\', '/').Equals(
        $RepositoryRoot.TrimEnd('\', '/'),
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw "Bake Output must not replace the repository root: $fullDestination"
    }
    foreach ($protectedRoot in $protectedRoots) {
        if (Test-PathWithin -Path $fullDestination -Root $protectedRoot) {
            throw "Bake Output must not replace protected repository content: $fullDestination"
        }
    }
    if (Test-Path -LiteralPath $fullDestination) {
        $allowedExistingRoots = @(
            (Join-Path $RepositoryRoot "staging"),
            (Join-Path $PackageRoot "renders")
        )
        $isInAllowedRoot = $false
        foreach ($allowedRoot in $allowedExistingRoots) {
            if (Test-PathWithin -Path $fullDestination -Root $allowedRoot) {
                $isInAllowedRoot = $true
                break
            }
        }
        $ownershipSentinel = Join-Path $fullDestination $BakeOwnershipSentinelName
        if (-not $isInAllowedRoot -and -not (Test-Path -LiteralPath $ownershipSentinel -PathType Leaf)) {
            throw "Existing Bake Output outside staging/package renders requires ownership sentinel '$BakeOwnershipSentinelName': $fullDestination"
        }
    }
}

function New-ReleaseStagingPath {
    param([Parameter(Mandatory = $true)][string]$Destination)
    $fullDestination = [IO.Path]::GetFullPath($Destination)
    $parent = Split-Path -Parent $fullDestination
    $leaf = Split-Path -Leaf $fullDestination
    return Join-Path $parent ".$leaf.release-$([Guid]::NewGuid().ToString('N'))"
}

function Move-ReleaseDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$StagingPath,
        [Parameter(Mandatory = $true)][string]$Destination,
        [switch]$SimulateFailureAfterBackup
    )
    $failureIndex = if ($SimulateFailureAfterBackup) { 0 } else { -1 }
    Publish-ReleaseTransaction -Items @(
        [pscustomobject]@{
            StagingPath = $StagingPath
            Destination = $Destination
        }
    ) -SimulateFailureAtIndex $failureIndex
}

function Add-PreparedReleaseItem {
    param(
        [Parameter(Mandatory = $true)][string]$StagingPath,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    $script:PreparedReleaseItems += [pscustomobject]@{
        StagingPath = [IO.Path]::GetFullPath($StagingPath)
        Destination = [IO.Path]::GetFullPath($Destination)
    }
}

function Remove-PreparedReleaseStaging {
    if ($DryRun) {
        return
    }
    foreach ($item in $script:PreparedReleaseItems) {
        if (Test-Path -LiteralPath $item.StagingPath) {
            Remove-Item -LiteralPath $item.StagingPath -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Publish-ReleaseTransaction {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Items,
        [int]$SimulateFailureAtIndex = -1
    )
    if ($Items.Count -eq 0) {
        return
    }
    if ($DryRun) {
        foreach ($item in $Items) {
            Write-Host "[dry-run] atomically promote $($item.StagingPath) -> $($item.Destination) (transaction rollback on failure)"
        }
        return
    }
    foreach ($item in $Items) {
        if (-not (Test-Path -LiteralPath $item.StagingPath -PathType Container)) {
            throw "Release staging directory does not exist: $($item.StagingPath)"
        }
    }

    $states = [Collections.Generic.List[object]]::new()
    try {
        for ($index = 0; $index -lt $Items.Count; $index++) {
            $item = $Items[$index]
            $backupPath = New-ReleaseStagingPath -Destination "$($item.Destination).rollback"
            $state = [pscustomobject]@{
                StagingPath = $item.StagingPath
                Destination = $item.Destination
                BackupPath = $backupPath
                HadPrevious = Test-Path -LiteralPath $item.Destination
                Promoted = $false
            }
            $states.Add($state)
            New-Item -ItemType Directory -Path (Split-Path -Parent $item.Destination) -Force |
                Out-Null
            if ($state.HadPrevious) {
                Move-Item -LiteralPath $item.Destination -Destination $backupPath
            }
            if ($index -eq $SimulateFailureAtIndex) {
                throw "Simulated promotion failure at transaction index $index."
            }
            Move-Item -LiteralPath $item.StagingPath -Destination $item.Destination
            $state.Promoted = $true
        }
    } catch {
        $promotionError = $_
        $rollbackErrors = @()
        for ($index = $states.Count - 1; $index -ge 0; $index--) {
            $state = $states[$index]
            try {
                if ($state.Promoted -and (Test-Path -LiteralPath $state.Destination)) {
                    Move-Item -LiteralPath $state.Destination -Destination $state.StagingPath
                } elseif (Test-Path -LiteralPath $state.Destination) {
                    Remove-Item -LiteralPath $state.Destination -Recurse -Force
                }
                if ($state.HadPrevious -and (Test-Path -LiteralPath $state.BackupPath)) {
                    Move-Item -LiteralPath $state.BackupPath -Destination $state.Destination
                }
            } catch {
                $rollbackErrors += "$($state.Destination): $($_.Exception.Message)"
            }
        }
        if ($rollbackErrors.Count -gt 0) {
            throw "Release promotion failed: $($promotionError.Exception.Message). Rollback failures: $($rollbackErrors -join '; ')"
        }
        throw $promotionError
    }

    foreach ($state in $states) {
        if (Test-Path -LiteralPath $state.BackupPath) {
            try {
                Remove-Item -LiteralPath $state.BackupPath -Recurse -Force
            } catch {
                Write-Warning "Release transaction succeeded, but rollback cleanup failed: $($state.BackupPath)"
            }
        }
    }
}

function Get-GitCommit {
    $commit = (& git.exe -C $RepositoryRoot rev-parse HEAD)
    if ($LASTEXITCODE -ne 0 -or -not $commit) {
        throw "Unable to resolve git commit for release manifest."
    }
    return $commit.Trim()
}

function Test-GitDirty {
    $status = @(& git.exe -C $RepositoryRoot status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect git status for release manifest."
    }
    return $status.Count -gt 0
}

function Assert-SourceStateUnchanged {
    if ($null -eq $script:SourceCommit) {
        $script:SourceCommit = Get-GitCommit
    }
    $currentCommit = Get-GitCommit
    if ($currentCommit -ne $script:SourceCommit) {
        throw "Git HEAD changed during release: $($script:SourceCommit) -> $currentCommit"
    }
    if ($script:SourceWasDirty -eq $false -and (Test-GitDirty)) {
        throw "Git worktree changed during a clean release; refusing to write a misleading manifest."
    }
}

function Write-ReleaseManifest {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ReleaseTarget
    )
    $manifestPath = Join-Path $Root "release-manifest.json"
    if ($DryRun) {
        Write-Host "[dry-run] write release-manifest.json target=$ReleaseTarget with git commit and per-file SHA256"
        return
    }
    Assert-SourceStateUnchanged
    $files = @(
        Get-ChildItem -LiteralPath $Root -File -Recurse |
            Where-Object { $_.FullName -ne $manifestPath } |
            Sort-Object FullName |
            ForEach-Object {
                [ordered]@{
                    path = $_.FullName.Substring($Root.Length).TrimStart(
                        [char[]]@('\', '/')
                    ).Replace('\', '/')
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
    )
    $manifest = [ordered]@{
        schemaVersion = "1.0.0"
        generatedAtUtc = [DateTime]::UtcNow.ToString("o")
        gitCommit = if ($null -ne $script:SourceCommit) {
            $script:SourceCommit
        } else {
            Get-GitCommit
        }
        dirty = if ($null -eq $script:SourceWasDirty) {
            Test-GitDirty
        } else {
            $script:SourceWasDirty
        }
        target = $ReleaseTarget
        files = $files
    }
    [IO.File]::WriteAllText(
        $manifestPath,
        ($manifest | ConvertTo-Json -Depth 5),
        [Text.UTF8Encoding]::new($false)
    )
}

function Format-Command {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $formatted = @($FilePath) + @($Arguments) | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }
    return $formatted -join " "
}

function Invoke-ReleaseCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$WorkingDirectory = $RepositoryRoot
    )
    $display = Format-Command -FilePath $FilePath -Arguments $Arguments
    if ($DryRun) {
        Write-Host "[dry-run][$WorkingDirectory] $display"
        return
    }
    Write-Host "[run][$WorkingDirectory] $display"
    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code $LASTEXITCODE`: $display"
        }
    } finally {
        Pop-Location
    }
}

function Invoke-ReleaseProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$WorkingDirectory = $RepositoryRoot
    )
    $display = Format-Command -FilePath $FilePath -Arguments $Arguments
    if ($DryRun) {
        Write-Host "[dry-run-wait][$WorkingDirectory] $display"
        return
    }
    $argumentLine = @($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '(\\*)"', '$1$1\"') + '"'
        } else {
            $_
        }
    }) -join " "
    Write-Host "[run-wait][$WorkingDirectory] $display"
    $process = Start-Process -FilePath $FilePath `
        -ArgumentList $argumentLine `
        -WorkingDirectory $WorkingDirectory `
        -PassThru `
        -Wait
    if ($process.ExitCode -ne 0) {
        throw "Process failed with exit code $($process.ExitCode): $display"
    }
}

function Assert-ReleaseInputs {
    param([Parameter(Mandatory = $true)][string[]]$SelectedTargets)

    $allowedTargets = @("UE", "ServerWeb", "BakeWeb", "All")
    foreach ($selectedTarget in $SelectedTargets) {
        if ($allowedTargets -notcontains $selectedTarget) {
            throw "Unknown Target '$selectedTarget'; supported values: UE, ServerWeb, BakeWeb, All."
        }
    }
    $validatedTargets = if ($SelectedTargets -contains "All") {
        @("UE", "ServerWeb", "BakeWeb")
    } else {
        $SelectedTargets
    }
    if ($IncludeRenders -and $validatedTargets -notcontains "ServerWeb") {
        throw "IncludeRenders is only valid when Target includes ServerWeb."
    }
    if ($Shard -notmatch '^(\d+)/([1-9]\d*)$') {
        throw "Shard must use <index>/<count>, for example 0/4."
    }
    $shardIndex = [int]$Matches[1]
    $shardCount = [int]$Matches[2]
    if ($shardIndex -ge $shardCount) {
        throw "Shard index must be less than shard count."
    }
    if ($Mode -eq "shard" -and $Shard -eq "0/1") {
        Write-Host "[release] shard mode uses the default single shard 0/1."
    }
    if ($Mode -ne "shard" -and $Shard -ne "0/1") {
        throw "A non-default Shard is only valid when Mode=shard."
    }
    if ($Publication -notmatch '^[a-z0-9]+(?:-[a-z0-9]+)*$') {
        throw "Publication must be a lowercase kebab-case stable ID."
    }

    if (-not $DryRun) {
        if (-not $AllowDirty -and (Test-GitDirty)) {
            throw "Release requires a clean Git worktree. Commit changes or pass -AllowDirty for an explicitly non-reproducible local build."
        }
        $requiredPaths = @()
        if ($validatedTargets -contains "UE" -or $validatedTargets -contains "ServerWeb") {
            $requiredPaths += @($WebRoot, $WebDeployScript)
        }
        if ($validatedTargets -contains "ServerWeb" -or $validatedTargets -contains "BakeWeb") {
            $requiredPaths += $ServerRoot
        }
        if ($validatedTargets -contains "UE" -or $validatedTargets -contains "BakeWeb") {
            $requiredPaths += $ProjectPath
        }
        foreach ($requiredPath in $requiredPaths) {
            if (-not (Test-Path -LiteralPath $requiredPath)) {
                throw "Missing release input: $requiredPath"
            }
        }
        if ($validatedTargets -contains "UE" -or $validatedTargets -contains "BakeWeb") {
            $uprojectJson = [IO.File]::ReadAllText(
                $ProjectPath,
                [Text.Encoding]::UTF8
            )
            $uproject = $uprojectJson | ConvertFrom-Json
            if ($uproject.EngineAssociation -ne "5.8") {
                throw "Project EngineAssociation must be 5.8; actual: $($uproject.EngineAssociation)."
            }
        }
        if ($IncludeRenders -and -not (Test-Path -LiteralPath (Join-Path $PackageRoot "renders"))) {
            throw "IncludeRenders requires an existing package/renders directory."
        }
    }
}

function Invoke-Tests {
    param([Parameter(Mandatory = $true)][string[]]$SelectedTargets)
    if ($SkipTests) {
        Write-Host "[skip] SkipTests: release tests are skipped."
        return
    }

    Invoke-ReleaseCommand "node.exe" @(
        (Join-Path $RepositoryRoot "harness\validate.mjs")
    )
    Invoke-ReleaseCommand "node.exe" @(
        (Join-Path $RepositoryRoot "tools\validate-contracts.mjs")
    )
    if ($SelectedTargets -contains "UE" -or $SelectedTargets -contains "ServerWeb") {
        Invoke-ReleaseCommand "npm.cmd" @("test") $WebRoot
    }
    if ($SelectedTargets -contains "ServerWeb" -or $SelectedTargets -contains "BakeWeb") {
        Invoke-ReleaseCommand "npm.cmd" @("test") $ServerRoot
    }
    if ($SelectedTargets -contains "BakeWeb") {
        Invoke-ReleaseCommand "node.exe" @(
            "--test",
            (Join-Path $RepositoryRoot "tools\generate-published-configurations.test.mjs")
        )
    }
    if ($SelectedTargets -contains "UE") {
        $editorCmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
        Invoke-ReleaseCommand $editorCmd @(
            $ProjectPath,
            "-Unattended",
            "-NullRHI",
            "-NoSplash",
            "-NoSound",
            "-ExecCmds=Automation RunTests ConfigurationSystem.Runtime;Quit",
            "-TestExit=Automation Test Queue Empty",
            "-log"
        )
    }
}

function Build-Web {
    param(
        [Parameter(Mandatory = $true)][string]$OutputRoot,
        [Parameter(Mandatory = $true)][string]$EmbeddedOutputRoot
    )
    Invoke-ReleaseCommand "npm.cmd" @("exec", "tsc", "--", "-b") $WebRoot
    Invoke-ReleaseCommand "npm.cmd" @(
        "exec", "vite", "--", "build", "--outDir", $OutputRoot, "--emptyOutDir"
    ) $WebRoot
    Invoke-ReleaseCommand "node.exe" @(
        $WebDeployScript,
        "--web-package", $OutputRoot,
        "--embedded-package", $EmbeddedOutputRoot
    ) $WebRoot
}

function Get-WebArtifact {
    if ($null -ne $script:WebArtifact) {
        Write-Host "[release] reuse shared Web artifact"
        return
    }
    $artifactRoot = New-ReleaseStagingPath -Destination (Join-Path $PackageRoot "web-artifact")
    $onlineRoot = Join-Path $artifactRoot "online"
    $embeddedRoot = Join-Path $artifactRoot "embedded"
    if (-not $DryRun) {
        New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
    } else {
        Write-Host "[dry-run] build one shared Web artifact: $artifactRoot"
    }
    Build-Web -OutputRoot $onlineRoot -EmbeddedOutputRoot $embeddedRoot
    $script:WebArtifact = [pscustomobject]@{
        Root = $artifactRoot
        Online = $onlineRoot
        Embedded = $embeddedRoot
    }
}

function Remove-WebArtifact {
    if (
        -not $DryRun -and
        $null -ne $script:WebArtifact -and
        (Test-Path -LiteralPath $script:WebArtifact.Root)
    ) {
        Remove-Item -LiteralPath $script:WebArtifact.Root -Recurse -Force
    }
    $script:WebArtifact = $null
}

function Build-ServerWeb {
    $bundleRoot = Join-Path $PackageRoot "server-web"
    $stagingRoot = New-ReleaseStagingPath -Destination $bundleRoot
    Add-PreparedReleaseItem -StagingPath $stagingRoot -Destination $bundleRoot
    $bundlePackageRoot = Join-Path $stagingRoot "package"
    $bundleServerRoot = Join-Path $bundlePackageRoot "server"
    $bundleWebRoot = Join-Path $bundlePackageRoot "clients\web"
    Get-WebArtifact
    $webArtifact = $script:WebArtifact

    if ($DryRun) {
        Write-Host "[dry-run] stage ServerWeb on destination volume: $stagingRoot"
    } else {
        New-Item -ItemType Directory -Path $bundleServerRoot -Force | Out-Null
        New-Item -ItemType Directory -Path (Split-Path -Parent $bundleWebRoot) -Force |
            Out-Null
        Copy-Item -LiteralPath $webArtifact.Online -Destination $bundleWebRoot -Recurse
    }
    if ($DryRun) {
        Write-Host "[dry-run] copy shared online Web artifact -> $bundleWebRoot"
    }
    Invoke-ReleaseCommand "npm.cmd" @(
        "exec", "tsc", "--", "-p", "tsconfig.json",
        "--outDir", (Join-Path $bundleServerRoot "dist")
    ) $ServerRoot

    if ($DryRun) {
        $rendersPlan = if ($IncludeRenders) { "including renders" } else { "without renders (use -IncludeRenders to opt in)" }
        Write-Host "[dry-run] assemble portable bundle $rendersPlan"
    } else {
        Copy-Item -LiteralPath (Join-Path $ServerRoot "package.json") `
            -Destination $bundleServerRoot
        Copy-Item -LiteralPath (Join-Path $ServerRoot "package-lock.json") `
            -Destination $bundleServerRoot
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot "contracts") `
            -Destination (Join-Path $bundlePackageRoot "contracts") -Recurse
        if ($IncludeRenders) {
            $rendersRoot = Join-Path $PackageRoot "renders"
            Copy-Item -LiteralPath $rendersRoot `
                -Destination (Join-Path $bundlePackageRoot "renders") -Recurse
        }
    }

    Invoke-ReleaseCommand "npm.cmd" @(
        "ci",
        "--omit=dev",
        "--ignore-scripts"
    ) $bundleServerRoot

    $launcherPath = Join-Path $stagingRoot "start-server.ps1"
    if ($DryRun) {
        Write-Host "[dry-run] write portable launcher $launcherPath"
    } else {
        $launcher = @'
[CmdletBinding()]
param(
    [int]$Port = 8080,
    [string]$HostAddress = "0.0.0.0"
)
$ErrorActionPreference = "Stop"
$env:PORT = [string]$Port
$env:HOST = $HostAddress
$env:CONFIGURATION_STORE_V2_PATH = Join-Path ([Environment]::GetFolderPath("LocalApplicationData")) "ConfigurationSystem/data/configurations-v2.json"
New-Item -ItemType Directory -Path (Split-Path -Parent $env:CONFIGURATION_STORE_V2_PATH) -Force | Out-Null
Set-Location -LiteralPath $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot "package/renders"))) {
    $env:BAKE_ROOT = Join-Path $PSScriptRoot "package/contracts/fixtures/bake.valid"
}
& node.exe "package/server/dist/src/index.js"
exit $LASTEXITCODE
'@
        [IO.File]::WriteAllText(
            $launcherPath,
            $launcher,
            [Text.UTF8Encoding]::new($false)
        )
    }
    Write-ReleaseManifest -Root $stagingRoot -ReleaseTarget "ServerWeb"
    Write-Host "[release] ServerWeb prepared: $stagingRoot"
}

function Build-UE {
    $build = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
    $uat = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
    $archive = Join-Path $PackageRoot "clients\ue"
    $stagingArchive = New-ReleaseStagingPath -Destination $archive
    $stagingEmbedded = New-ReleaseStagingPath -Destination $EmbeddedWebRoot
    $embeddedBackup = New-ReleaseStagingPath -Destination "$EmbeddedWebRoot.rollback"
    $hadEmbedded = Test-Path -LiteralPath $EmbeddedWebRoot
    $embeddedActivated = $false
    Add-PreparedReleaseItem -StagingPath $stagingEmbedded -Destination $EmbeddedWebRoot
    Add-PreparedReleaseItem -StagingPath $stagingArchive -Destination $archive
    Get-WebArtifact
    $webArtifact = $script:WebArtifact

    if ($DryRun) {
        Write-Host "[dry-run] stage UE archive on destination volume: $stagingArchive"
    }
    if ($DryRun) {
        Write-Host "[dry-run] copy shared embedded Web artifact -> $stagingEmbedded"
    } else {
        Copy-Item -LiteralPath $webArtifact.Embedded `
            -Destination $stagingEmbedded -Recurse
    }
    $prepareError = $null
    try {
        if ($DryRun) {
            Write-Host "[dry-run] temporarily activate staged embedded WebUI for UE build, then restore source tree"
        } else {
            if ($hadEmbedded) {
                Move-Item -LiteralPath $EmbeddedWebRoot -Destination $embeddedBackup
            }
            Move-Item -LiteralPath $stagingEmbedded -Destination $EmbeddedWebRoot
            $embeddedActivated = $true
        }
        Invoke-ReleaseCommand $build @(
            "ConfigurationSystem",
            "Win64",
            "Shipping",
            "-Project=$ProjectPath",
            "-WaitMutex",
            "-NoHotReloadFromIDE",
            "-gather"
        )
        Invoke-ReleaseCommand $uat @(
            "BuildCookRun",
            "-Project=$ProjectPath",
            "-noP4",
            "-platform=Win64",
            "-clientconfig=Shipping",
            "-build",
            "-cook",
            "-stage",
            "-pak",
            "-iostore",
            "-archive",
            "-archivedirectory=$stagingArchive",
            "-unattended",
            "-utf8output"
        )
        Write-ReleaseManifest -Root $stagingArchive -ReleaseTarget "UE"
    } catch {
        $prepareError = $_
    } finally {
        if (-not $DryRun -and $embeddedActivated) {
            if (Test-Path -LiteralPath $stagingEmbedded) {
                Remove-Item -LiteralPath $stagingEmbedded -Recurse -Force
            }
            Move-Item -LiteralPath $EmbeddedWebRoot -Destination $stagingEmbedded
            if ($hadEmbedded -and (Test-Path -LiteralPath $embeddedBackup)) {
                Move-Item -LiteralPath $embeddedBackup -Destination $EmbeddedWebRoot
            }
        }
    }
    if ($null -ne $prepareError) {
        throw $prepareError
    }
    Write-Host "[release] UE prepared: $stagingArchive"
}

function Build-BakeWeb {
    $inputPath = Resolve-RepositoryPath $BakeInput
    $destinationRoot = Resolve-RepositoryPath $BakeOutput
    Assert-SafeBakeDestination -Destination $destinationRoot
    $outputRoot = if ($Mode -eq "estimate") {
        $destinationRoot
    } else {
        New-ReleaseStagingPath -Destination $destinationRoot
    }
    if ($Mode -ne "estimate") {
        Add-PreparedReleaseItem -StagingPath $outputRoot -Destination $destinationRoot
    }
    $planPath = Join-Path $outputRoot "published-configurations.json"
    $manifestPath = Join-Path $outputRoot "bake-manifest.json"
    $generator = Join-Path $RepositoryRoot "tools\generate-published-configurations.mjs"

    if (-not $DryRun -and -not (Test-Path -LiteralPath $inputPath)) {
        throw "Bake Input does not exist: $inputPath"
    }
    if (-not $DryRun -and $Mode -ne "estimate") {
        New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    } elseif ($DryRun -and $Mode -ne "estimate") {
        Write-Host "[dry-run] stage BakeWeb on destination volume: $outputRoot"
    }

    $generateArguments = @(
        $generator,
        $inputPath,
        $planPath,
        $Publication,
        "--mode",
        $Mode
    )
    if ($Mode -eq "shard") {
        $generateArguments += @("--shard", $Shard)
    }
    Invoke-ReleaseCommand "node.exe" $generateArguments

    if ($Mode -eq "estimate") {
        Write-Host "[release] BakeWeb estimate completed; UE Bake and validation are skipped."
        return
    }

    $editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
    Invoke-ReleaseProcess $editor @(
        $ProjectPath,
        "/Game/Maps/L_ConfigShowroom",
        "-game",
        "-windowed",
        "-dx12",
        "-raytracing",
        "-RenderOffscreen",
        "-ForceRes",
        "-ConfigurationBatchBake",
        "-ConfigurationBakeProfile=$Profile",
        "-ConfigurationBakeInput=$planPath",
        "-ConfigurationBakeStaging=$outputRoot",
        "-log"
    )
    Invoke-ReleaseCommand "npm.cmd" @(
        "run",
        "validate:bake",
        "--",
        $manifestPath,
        $outputRoot
    ) $ServerRoot
    if (-not $DryRun) {
        [IO.File]::WriteAllText(
            (Join-Path $outputRoot $BakeOwnershipSentinelName),
            "Owned by ConfigurationSystem BakeWeb release tooling.`n",
            [Text.UTF8Encoding]::new($false)
        )
    }
    Write-ReleaseManifest -Root $outputRoot -ReleaseTarget "BakeWeb"
    Write-Host "[release] BakeWeb prepared and validated: $outputRoot"
}

if ($env:RELEASE_PS1_IMPORT_ONLY -ne "1") {
$selectedTargets = @(
    foreach ($targetValue in $Target) {
        foreach ($item in ($targetValue -split '[,+]')) {
            $trimmed = $item.Trim()
            if ($trimmed) {
                $trimmed
            }
        }
    }
)
if ($selectedTargets.Count -eq 0) {
    throw "At least one Target is required."
}
Assert-ReleaseInputs -SelectedTargets $selectedTargets
$script:SourceCommit = Get-GitCommit
$script:SourceWasDirty = Test-GitDirty
if ($selectedTargets -contains "All") {
    $selectedTargets = @("UE", "ServerWeb", "BakeWeb")
} else {
    $selectedTargets = @(
        "UE", "ServerWeb", "BakeWeb" |
            Where-Object { $selectedTargets -contains $_ }
    )
}

Write-Host "[release] targets=$($selectedTargets -join ',') dryRun=$DryRun skipTests=$SkipTests"
Invoke-Tests -SelectedTargets $selectedTargets

try {
    if ($selectedTargets -contains "ServerWeb") {
        Build-ServerWeb
    }
    if ($selectedTargets -contains "UE") {
        Build-UE
    }
    if ($selectedTargets -contains "BakeWeb") {
        Build-BakeWeb
    }
    Publish-ReleaseTransaction -Items $script:PreparedReleaseItems
    if ($script:PreparedReleaseItems.Count -gt 0) {
        Write-Host "[release] promoted targets=$($selectedTargets -join ',')"
    } else {
        Write-Host "[release] completed without promotable artifacts"
    }
} catch {
    if ($CleanFailedStaging) {
        Remove-PreparedReleaseStaging
    } else {
        foreach ($item in $script:PreparedReleaseItems) {
            if (Test-Path -LiteralPath $item.StagingPath) {
                Write-Warning "Release failed; staging preserved for diagnosis: $($item.StagingPath)"
            }
        }
    }
    throw
} finally {
    Remove-WebArtifact
}

Write-Host "[release] completed targets=$($selectedTargets -join ',')"
}
