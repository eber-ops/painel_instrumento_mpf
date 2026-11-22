# Verificação do Trampoline em WSL1

Execução em 7 de novembro de 2026.

## Ambiente

- Windows com virtualização de firmware desativada.
- Distribuição Ubuntu 26.04.1 LTS em WSL1.
- Trampoline oficial no commit 09e2dcb1206fb6ebaa73c84558f26ac21f57b2dd.
- GOIL compilado a partir do código-fonte.
- ViPER compilado a partir do código-fonte.
- GCC/G++, Make e Python instalados no Ubuntu.

## Resultados

| Exemplo oficial | Resultado |
| --- | --- |
| examples/posix/periodic | Nove ativações, Shutdown, encerramento com código 0 |
| examples/posix/events | Dez ativações, encerramento com código 0 |

Os dois exemplos usaram configuração OIL gerada pelo GOIL com o alvo
posix/linux e limite externo de execução de 20 segundos. Ambos encerraram
normalmente antes desse limite.

Os testes confirmam a execução do kernel Trampoline POSIX, do temporizador
ViPER e dos exemplos de alarmes e eventos em WSL1 neste computador.
A conexão com o painel Qt está implementada em trampoline/. A suíte CTest
simulator_trampoline passou usando o backend real, incluindo partida,
desligamento, periodicidade, falhas, timeout e reinicialização do processo.

## Repetir os exemplos

No PowerShell, para o exemplo periódico:

```powershell
wsl -d Ubuntu -u root -- bash -lc 'cd /root/trampoline-test/source/examples/posix/periodic && export VIPER_PATH=/root/trampoline-test/source/viper && timeout 20s ./periodic_exe'
```

Para o exemplo de eventos:

```powershell
wsl -d Ubuntu -u root -- bash -lc 'cd /root/trampoline-test/source/examples/posix/events && export VIPER_PATH=/root/trampoline-test/source/viper && timeout 20s ./events_exe'
```

Os logs completos ficam em /root/trampoline-test no Ubuntu. O tar do
Ubuntu retornou Function not implemented em operações de extração no WSL1.
A extração do código foi concluída com tarfile do Python.

Fonte oficial: https://github.com/TrampolineRTOS/trampoline
