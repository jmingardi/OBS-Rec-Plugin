[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductDisplayName = if ( $BuildSpec.displayName ) { $BuildSpec.displayName } else { $ProductName }
    $ProductVersion = $BuildSpec.version
    $ProductPublisher = $BuildSpec.author
    $ProductWebsite = $BuildSpec.website.TrimEnd('/')

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $StageDir = "${ProjectRoot}/release/${Configuration}"
    $PluginBinary = "${StageDir}/${ProductName}/bin/64bit/${ProductName}.dll"

    if ( ! ( Test-Path -LiteralPath $PluginBinary -PathType Leaf ) ) {
        throw "Plugin binary not found at '${PluginBinary}'. Run the CMake build and install steps first."
    }

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
            "${ProjectRoot}/release/${ProductName}-*-windows-*.exe"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path $StageDir -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    Log-Group "Building ${ProductDisplayName} installer..."
    $IsccCandidates = @(
        ( Get-Command iscc.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1 )
        "${env:LOCALAPPDATA}/Programs/Inno Setup 6/ISCC.exe"
        "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe"
        "${env:ProgramFiles}/Inno Setup 6/ISCC.exe"
    )
    $Iscc = $IsccCandidates | Where-Object { $_ -and ( Test-Path -LiteralPath $_ -PathType Leaf ) } |
        Select-Object -First 1
    if ( ! $Iscc ) {
        throw 'Inno Setup compiler (ISCC.exe) was not found.'
    }

    $InstallerScript = "${ScriptHome}/installer-Windows.iss"
    $InstallerArgs = @(
        "/DPluginName=${ProductName}"
        "/DPluginDisplayName=${ProductDisplayName}"
        "/DPluginVersion=${ProductVersion}"
        "/DPluginPublisher=${ProductPublisher}"
        "/DPluginWebsite=${ProductWebsite}"
        "/DStageDir=${StageDir}"
        "/DSourceRoot=${ProjectRoot}"
        "/DOutputDir=${ProjectRoot}/release"
        "/DOutputName=${OutputName}-Installer"
        $InstallerScript
    )
    & $Iscc @InstallerArgs
    if ( $LASTEXITCODE -ne 0 ) {
        throw "Inno Setup failed with exit code ${LASTEXITCODE}."
    }
    Log-Group
}

Package
