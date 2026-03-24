$ErrorActionPreference = 'Stop'

$gcc = 'C:\Users\gabri\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe'

if (!(Test-Path $gcc)) {
    throw "GCC não encontrado em: $gcc"
}

& $gcc -std=c11 -O2 -Wall -Wextra -pedantic -pthread main.c ticket_system.c -o ticket_sim.exe
if ($LASTEXITCODE -ne 0) {
    throw 'Falha na compilação.'
}

Write-Host "\n=== Rodando: .\\ticket_sim.exe 10 1000 20 ===\n"
.\ticket_sim.exe 10 1000 20

Write-Host "\n=== Rodando: .\\ticket_sim.exe 10 10000 20 ===\n"
.\ticket_sim.exe 10 10000 20
