[CmdletBinding()]
param([switch]$SkipBuild, [string]$CompilerPath, [switch]$TestInstaller)
# HPDG 808 installer (VST3 + Standalone): Releases\HPDG808_Setup_<version>.exe and its SHA-256.
# Needs CMake and Visual Studio C++; the signed VC++ Runtime and Inno Setup 6.7.3 are fetched into build\ by
# Installer\Build-Beta.ps1's rules (the same files are reused when they are there).
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path $PSScriptRoot -Parent
Push-Location $repoDir
try {
    $cmakeText = Get-Content -LiteralPath (Join-Path $repoDir 'CMakeLists.txt') -Raw
    if ($cmakeText -notmatch 'set\(HPDG808_VERSION (\d+)\.(\d+)\.(\d+)\)') { throw 'set(HPDG808_VERSION x.y.z) not found in CMakeLists.txt.' }
    $version = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
    $fileVersion = "$version.0"
    Write-Output "HPDG 808 version: $version"

    $releaseDir = Join-Path $repoDir 'Releases'
    $toolDir = Join-Path $repoDir 'build\tools'
    $runtimeFile = Join-Path $repoDir 'build\prerequisites\vc_redist.x64.exe'
    New-Item -ItemType Directory -Force $releaseDir, $toolDir, (Split-Path $runtimeFile) | Out-Null
    if (!(Test-Path -LiteralPath $runtimeFile)) {
        Invoke-WebRequest 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile $runtimeFile
    }
    $runtimeSignature = Get-AuthenticodeSignature -LiteralPath $runtimeFile
    if ($runtimeSignature.Status -ne 'Valid' -or $runtimeSignature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') {
        throw 'Visual C++ Runtime must have a valid Microsoft signature.'
    }
    if (!$CompilerPath) {
        $CompilerPath = Join-Path $toolDir 'InnoSetup\ISCC.exe'
        if (!(Test-Path -LiteralPath $CompilerPath)) {
            $compilerSetup = Join-Path $toolDir 'innosetup-6.7.3.exe'
            Invoke-WebRequest 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' -OutFile $compilerSetup
            $signature = Get-AuthenticodeSignature -LiteralPath $compilerSetup
            if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Pyrsys') { throw 'Invalid Inno Setup signature.' }
            $portableDir = Split-Path $CompilerPath
            $process = Start-Process -FilePath $compilerSetup -ArgumentList ('/PORTABLE=1 /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="' + $portableDir + '"') -WindowStyle Hidden -PassThru -Wait
            if ($process.ExitCode -ne 0) { throw "Inno Setup extraction failed: $($process.ExitCode)" }
        }
    }
    if (!$SkipBuild) {
        & cmake -S $repoDir -B build
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
        & cmake --build build --config Release --target HPDG808_VST3 HPDG808_Standalone --parallel 4
        if ($LASTEXITCODE -ne 0) { throw 'HPDG 808 build failed.' }
    }
    $bundleDir = Join-Path $repoDir 'build\HPDG808_artefacts\Release\VST3\HPDG 808.vst3'
    $standalone = Join-Path $repoDir 'build\HPDG808_artefacts\Release\Standalone\HPDG 808.exe'
    if (!(Test-Path -LiteralPath (Join-Path $bundleDir 'Contents\x86_64-win\HPDG 808.vst3'))) { throw 'VST3 binary missing.' }
    if (!(Test-Path -LiteralPath $standalone)) { throw 'Standalone missing.' }

    $compilerArgs = @('/Qp', "/DSourceVst3Dir=$bundleDir", "/DSourceStandalone=$standalone", "/DVCRedistFile=$runtimeFile",
        "/DMyAppVersion=$version", "/DMyVersionTag=$version", "/DMyFileVersion=$fileVersion")
    if ($TestInstaller) {
        $testOutput = Join-Path $repoDir 'build\installer-test-output'
        New-Item -ItemType Directory -Force $testOutput | Out-Null
        $compilerArgs += '/DInstallerTest=1', "/DOutputDir=$testOutput"
    }
    & $CompilerPath @compilerArgs (Join-Path $PSScriptRoot 'HPDG808_VST3.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
    if (!$TestInstaller) {
        $installer = Join-Path $releaseDir "HPDG808_Setup_$version.exe"
        $installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
        "$installerHash  $([IO.Path]::GetFileName($installer))" | Set-Content "$installer.sha256" -Encoding ascii
        Write-Output "HPDG 808 ready: $installer ($((Get-Item $installer).Length) bytes, sha256 $installerHash)"
    }
} finally { Pop-Location }
