# Installs Arachne for this user (%LOCALAPPDATA%\Programs\Arachne, Start menu and Desktop shortcuts named "Atelier - Arachne"), through install.bat.
$ErrorActionPreference = 'Stop'
& cmd /c "`"$PSScriptRoot\install.bat`" $args"
exit $LASTEXITCODE
