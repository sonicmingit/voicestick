param([Parameter(Mandatory)][string]$BuildTools, [Parameter(Mandatory)][string]$PlatformJar,
      [string]$Jdk=$env:JAVA_HOME)
$ErrorActionPreference='Stop'
function RunTool($exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE) { throw "$exe failed ($LASTEXITCODE)" }
}
$out=Join-Path $PSScriptRoot '../build/watch5'
$tmp=Join-Path $out ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force "$tmp/classes","$tmp/dex" | Out-Null
RunTool "$BuildTools/aapt2.exe" @('link','-o',"$tmp/base.apk",'--manifest',"$PSScriptRoot/AndroidManifest.xml",'-I',$PlatformJar)
$sources=@(Get-ChildItem "$PSScriptRoot/src/*.java" | ForEach-Object FullName)
RunTool "$Jdk/bin/javac.exe" (@('-encoding','UTF-8','-source','8','-target','8','-classpath',$PlatformJar,'-d',"$tmp/classes") + $sources)
RunTool "$Jdk/bin/jar.exe" @('cf',"$tmp/classes.jar",'-C',"$tmp/classes",'.')
RunTool "$Jdk/bin/java.exe" @('-cp',"$BuildTools/lib/d8.jar",'com.android.tools.r8.D8','--min-api','28','--lib',$PlatformJar,'--output',"$tmp/dex","$tmp/classes.jar")
RunTool "$Jdk/bin/jar.exe" @('uf',"$tmp/base.apk",'-C',"$tmp/dex",'classes.dex')
RunTool "$BuildTools/zipalign.exe" @('-f','4',"$tmp/base.apk","$tmp/aligned.apk")
$key="$out/watch-debug.keystore"
if (!(Test-Path $key)) {
    RunTool "$Jdk/bin/keytool.exe" @('-genkeypair','-keystore',$key,'-storepass','android','-keypass','android','-alias','watch','-keyalg','RSA','-validity','10000','-dname','CN=VoiceStick Watch Development')
}
$apk="$out/VoiceStick-Watch-0.1.1.apk"
RunTool "$Jdk/bin/java.exe" @('-jar',"$BuildTools/lib/apksigner.jar",'sign','--ks',$key,'--ks-pass','pass:android','--out',$apk,"$tmp/aligned.apk")
RunTool "$Jdk/bin/java.exe" @('-jar',"$BuildTools/lib/apksigner.jar",'verify',$apk)
RunTool "$BuildTools/zipalign.exe" @('-c','4',$apk)
Write-Output $apk

