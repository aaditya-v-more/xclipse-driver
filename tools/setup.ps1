<#
Sets up this project's portable toolchain in tools\, with no installers and no admin rights:
Android NDK r29, Python 3.13.16 with meson/ninja/mako/packaging/pyyaml/pymsi, and glslang 16.6.0.
It also installs ADB (platform-tools) to %USERPROFILE%\platform-tools when adb isn't on PATH.

Every download is verified: the NDK against Google's published SHA-1, glslang against GitHub's
SHA-256 digest, and python.exe and adb.exe by their Authenticode signatures. Tools already
present are skipped, so it's safe to run again.

usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
#>
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Tools = $PSScriptRoot
$Downloads = Join-Path $Tools '_downloads'

function Get-File($Url, $Name) {
   New-Item -ItemType Directory -Force -Path $Downloads | Out-Null
   $out = Join-Path $Downloads $Name
   curl.exe -sS --fail --location --retry 5 --retry-delay 5 -C - -o $out $Url
   if ($LASTEXITCODE -ne 0) { throw "download failed: $Url" }
   return $out
}

# Android NDK r29 (795 MB), the version the driver port is tested with.
$ndk = Join-Path $Tools 'android-ndk-r29'
if (Test-Path (Join-Path $ndk 'source.properties')) {
   'NDK r29: present'
} else {
   $zip = Get-File 'https://dl.google.com/android/repository/android-ndk-r29-windows.zip' 'android-ndk-r29-windows.zip'
   $xml = (Invoke-WebRequest 'https://dl.google.com/android/repository/repository2-3.xml' -UseBasicParsing).Content
   $want = [regex]::Match($xml, '<checksum[^>]*>([0-9a-f]{40})</checksum>\s*<url>android-ndk-r29-windows\.zip</url>').Groups[1].Value
   $got = (Get-FileHash $zip -Algorithm SHA1).Hash.ToLower()
   if (-not $want -or $want -ne $got) { throw "NDK checksum mismatch: Google publishes '$want', the download is '$got'" }
   tar.exe -xf $zip -C $Tools
   if ($LASTEXITCODE -ne 0) { throw 'unpacking the NDK failed' }
   Remove-Item -LiteralPath $zip
   "NDK r29: installed (SHA-1 $got matches Google's)"
}

# Python 3.13.16, the official NuGet package (14 MB).
$py = Join-Path $Tools 'python\python.exe'
if (Test-Path $py) {
   'Python: present'
} else {
   $pkg = Get-File 'https://api.nuget.org/v3-flatcontainer/python/3.13.16/python.3.13.16.nupkg' 'python.3.13.16.nupkg'
   $x = Join-Path $Downloads 'python-x'
   New-Item -ItemType Directory -Force -Path $x | Out-Null
   tar.exe -xf $pkg -C $x
   $sig = Get-AuthenticodeSignature (Join-Path $x 'tools\python.exe')
   if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'Python Software Foundation') {
      throw "python.exe signature check failed: $($sig.Status)"
   }
   Move-Item -LiteralPath (Join-Path $x 'tools') -Destination (Join-Path $Tools 'python')
   'Python 3.13.16: installed (signed by the Python Software Foundation)'
}
& $py -m pip install --disable-pip-version-check --no-warn-script-location --quiet meson ninja mako packaging pyyaml pymsi
if ($LASTEXITCODE -ne 0) { throw 'pip install failed' }
'Python packages: meson, ninja, mako, packaging, pyyaml, pymsi'

# glslang 16.6.0 (13 MB). Mesa's build calls it glslangValidator.
$gv = Join-Path $Tools 'glslang\bin\glslangValidator.exe'
if (Test-Path $gv) {
   'glslang: present'
} else {
   $name = 'glslang-16.6.0-windows-x86_64-release.zip'
   $zip = Get-File "https://github.com/KhronosGroup/glslang/releases/download/16.6.0/$name" $name
   $rel = Invoke-RestMethod 'https://api.github.com/repos/KhronosGroup/glslang/releases/tags/16.6.0' -Headers @{ 'User-Agent' = 'xclipse-driver-setup' }
   $want = ($rel.assets | Where-Object { $_.name -eq $name }).digest
   $got = 'sha256:' + (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
   if ($want -ne $got) { throw "glslang checksum mismatch: GitHub publishes '$want', the download is '$got'" }
   $dest = Join-Path $Tools 'glslang'
   New-Item -ItemType Directory -Force -Path $dest | Out-Null
   tar.exe -xf $zip -C $dest
   Copy-Item -LiteralPath (Join-Path $dest 'bin\glslang.exe') -Destination $gv
   Remove-Item -LiteralPath $zip
   "glslang 16.6.0: installed ($got matches GitHub's)"
}

# ADB, unless one is already on PATH.
if (Get-Command adb -ErrorAction SilentlyContinue) {
   'adb: on PATH'
} else {
   $pt = Join-Path $env:USERPROFILE 'platform-tools'
   if (-not (Test-Path (Join-Path $pt 'adb.exe'))) {
      $zip = Get-File 'https://dl.google.com/android/repository/platform-tools-latest-windows.zip' 'platform-tools.zip'
      tar.exe -xf $zip -C $env:USERPROFILE
      Remove-Item -LiteralPath $zip
   }
   $sig = Get-AuthenticodeSignature (Join-Path $pt 'adb.exe')
   if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'Google LLC') { throw 'adb.exe signature check failed' }
   "adb: $pt\adb.exe (signed by Google LLC). pipeline\run.py finds it there; add it to PATH for your own shells."
}

if (Test-Path $Downloads) { Remove-Item -LiteralPath $Downloads -Recurse -Force }
'Done. Build the driver with: call tools\env.cmd && driver\build.cmd'
