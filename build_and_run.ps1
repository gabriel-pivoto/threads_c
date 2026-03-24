$GCC = "C:\Users\gabri\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
$EXE = "ticket_sim.exe"

# Wait a bit and try to clean
Write-Output "Preparando para compilação..."
Start-Sleep -Seconds 2

# Try to compile
Write-Output "Compilando..."
& $GCC -std=c11 -O2 -Wall -Wextra -pedantic -pthread main.c ticket_system.c -o $EXE

if ($LASTEXITCODE -eq 0) {
    Write-Output "Compilação bem-sucedida!"
    Write-Output ""
    Write-Output "Iniciando modo interativo..."
    & ".\$EXE"
} else {
    Write-Output "Falha na compilação (code: $LASTEXITCODE)"
}
