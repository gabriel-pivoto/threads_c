# Ticket Race Simulator

Simulador didático que demonstra **race conditions** em um sistema de venda de tickets com múltiplas threads.

## Como usar

### 1. **Via botão Run do VS Code** (padrão)

Clique em **Run** (F5) na barra de ação do VS Code. O programa rodará com os argumentos padrão: **10 tickets, 100 threads**.

Para alterar os valores:
- Abra [.vscode/launch.json](.vscode/launch.json)
- Procure por `"args": ["10", "100"]`
- Altere para `["tickets_desejado", "threads_desejado", "rows_to_show"]`
- Salve e clique em **Run** novamente

### 2. **Via Terminal** (linha de comando)

```bash
ticket_sim <tickets> <threads> [rows_to_show]
```

**Exemplos:**
```bash
./ticket_sim 10 100        # 10 tickets, 100 threads, mostra todos
./ticket_sim 10 1000 50    # 10 tickets, 1000 threads, mostra primeiras 50
./ticket_sim 5 5000        # 5 tickets, 5000 threads (muita concorrência)
```

### 3. **Via PowerShell** (incluso no repo)

```powershell
.\build_and_run.ps1
```

Compila automaticamente e roda com tamanho automático.

---

## O que esperar no output

- **Dashboard em tempo real**: mostra estado de cada thread (WAITING → CHECKING → REQUEST → DECREMENT → TICKET → SIGNING → SUCCESS/FAILED)
- **Coluna de etapas**: `CHK`, `REQ`, `DEC`, `TKT`, `SIG` - marcam quais etapas cada thread completou
- **Relatório de operação**: quantos threads, compras bem-sucedidas, falhas, oversold, estoque restante
- **Resumo final**: apenas os 5 campos-chave

---

## Tese Didática

O simulador demonstra que quando múltiplas threads executam a compra sem sincronização adequada:

1. **Stale snapshot**: thread lê estoque, mas aguarda antes de decrementar
2. **Many threads see positive stock**: múltiplas threads veem a mesma quantidade, decidem que há stock
3. **Oversell occurs**: mesmo com apenas 10 tickets, podem vender 30+, 100+, etc.
4. **Stock becomes negative**: estoque final fica negativo
5. **Receipts are issued anyway**: mesmo para compras inválidas, recebem assinatura "válida" no contexto local

---

## Compile

```bash
gcc -std=c11 -O2 -Wall -Wextra -pedantic -pthread main.c ticket_system.c -o ticket_sim.exe
```

Ou use a task do VS Code: **Ctrl+Shift+B** (build default)

---

## Arquivos

- `main.c` - entrada e parsing CLI
- `ticket_system.h` - interface de simulação
- `ticket_system.c` - lógica de race condition, dashboard ANSI, relatório
- `Makefile` - build com `make`
- `run_short.ps1` - atalho PowerShell
- `build_and_run.ps1` - script de build+run
- `.gitignore` - ignora binários, logs, temporários
- `README.md` - este arquivo

---

## Notas

- Não é um sistema real de e-commerce
- Assinatura é **apenas didática** e local (não é criptografia)
- **Sem garantia** de reproduzir oversell a cada execução (varia com timing da máquina)
- Para aumentar chance de oversell: eleve número de threads (ex: 10000)
