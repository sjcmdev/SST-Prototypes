param([switch]$SkipBuilds)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location $root
$lock = 'D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh'
$msbuild = 'D:/Aplications/VisualStudio/VisualStudioIDE/MSBuild/Current/Bin/MSBuild.exe'
$out = 'Prototypes/PlotLab/experiments/validation'
New-Item -ItemType Directory -Force $out | Out-Null
function Run-Check([string]$name, [string[]]$arguments) {
    & bash $lock @arguments 2>&1 | Tee-Object "$out/$name.log"
    if ($LASTEXITCODE -ne 0) { throw "$name failed ($LASTEXITCODE)" }
}
if (!$SkipBuilds) {
foreach ($target in @('PlotLab','PlotLabTests','JFMTests')) {
    Run-Check "build-$target" @($msbuild,"build/generated/vs2022/$target/$target.vcxproj",'-nr:false','-m:1','-p:Configuration=Debug','-p:Platform=x64','-v:minimal')
}
Run-Check 'build-engine-tests' @($msbuild,'../SST-Libraries/build/generated/vs2022/PlotLab-EngineTests/PlotLab-EngineTests.vcxproj','-nr:false','-m:1','-p:Configuration=Debug','-p:Platform=x64','-v:minimal')
}
foreach ($target in @('PlotLabTests','JFMTests')) {
    Run-Check "gtest-$target" @("build/bin/Debug-windows-x86_64/$target/$target.exe","--gtest_output=xml:$out/$target.xml")
}
Run-Check 'gtest-engine' @('../SST-Libraries/build/bin/Debug-windows-x86_64/PlotLab-EngineTests/PlotLab-EngineTests.exe',"--gtest_output=xml:$out/engine.xml")
Run-Check 'pytest-plotlab' @('.venv/Scripts/python.exe','-m','pytest','Prototypes/PlotLab/python/tests','-q',"--basetemp=$out/pytest-plotlab-temp",'-o',"cache_dir=$out/.pytest_cache","--junitxml=$out/pytest-plotlab.xml")
Run-Check 'pytest-jfm' @('.venv/Scripts/python.exe','-m','pytest','Prototypes/JFM/python/tests','-q',"--basetemp=$out/pytest-jfm-temp",'-o',"cache_dir=$out/.pytest_cache","--junitxml=$out/pytest-jfm.xml")
Run-Check 'smoke-plotlab' @('build/bin/Debug-windows-x86_64/PlotLab/PlotLab.exe','--smoke-test')
