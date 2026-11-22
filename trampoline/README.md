# Painel Qt conectado ao Trampoline

O modo **Trampoline (WSL1)** inicia o kernel real no Ubuntu e recebe seus
quadros CAN no painel. O seletor fica no topo da janela. **Reconectar**
reinicia o backend. O modo **Qt local** continua disponível no seletor.

## Backend

O arquivo panel.oil configura tarefas preemptivas, eventos, alarmes e o
recurso VehicleResource. Neste port, o SystemCounter tem tick de 10 ms.

| Tarefa | Ativação | Prioridade OIL |
| --- | --- | --- |
| Task_CAN_RX | Evento CanReady | 6 |
| Task_Acquisition | 10 ms | 5 |
| Task_Processing | 20 ms | 4 |
| Task_Display | 50 ms | 3 |
| Task_Diagnostics | 100 ms | 2 |
| Task_Logging | 1.000 ms | 1 |

No Trampoline, o maior número representa a maior prioridade. A aquisição
recebe os controles e coloca os quadros na fila. O processamento produz a
oscilação do motor. CAN_RX drena a fila quando recebe CanReady. Display
publica um snapshot interno a cada 50 ms e Diagnostics calcula os alertas.
Logging informa as contagens reais de execução na barra superior do Qt.
O painel desenha os valores dos quadros recebidos e mantém seu tratamento
de validação e timeout por sinal.

A porta de comandos é UDP em 127.0.0.1, escolhida pelo sistema. O backend
informa a porta ao Qt na inicialização. Os estados dos controles se repetem
a cada 20 ms. A saída padrão do processo entrega os quadros CAN e contagens
das tarefas ao Qt. A entrada padrão pertence ao teclado virtual do port
POSIX oficial. O Qt pede encerramento pelo canal de comandos ao fechar.

O backend usa CAN virtual, sem driver ou hardware CAN. Os períodos observados
dependem também do Windows, do WSL1 e do ViPER. Essa execução permite demonstrar
tarefas e eventos, sem comprovar limites de latência em hardware embarcado.

## Preparar em outro computador

O ambiente verificado usa Ubuntu WSL1, GCC, Make e Python 3. O código oficial
do Trampoline foi fixado no commit
09e2dcb1206fb6ebaa73c84558f26ac21f57b2dd.

No Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential python3 git
git clone https://github.com/TrampolineRTOS/trampoline.git ~/trampoline
cd ~/trampoline
git checkout 09e2dcb1206fb6ebaa73c84558f26ac21f57b2dd
cd goil/makefile-unix
python3 build.py release 4
cd ../../viper
make
```

No computador atual, a árvore preparada está em
/root/trampoline-test/source. Para gerar e compilar o aplicativo do painel,
execute no Ubuntu, ajustando o caminho até este repositório:

```bash
TRAMPOLINE_ROOT=/root/trampoline-test/source bash /caminho/do/projeto/trampoline/build.sh
```

O script copia panel.c e panel.oil para examples/posix/panel dentro da árvore
do Trampoline. O GOIL gera a configuração e make.py compila o kernel e o
aplicativo. O script encerra com erro se uma etapa falhar.

## Executar o Qt

A distribuição WSL deve se chamar Ubuntu. A versão atual inicia o backend
como root na distribuição de desenvolvimento. Para outra localização do
Trampoline, defina TRAMPOLINE_ROOT com o caminho Linux antes de abrir o painel.

```powershell
$env:TRAMPOLINE_ROOT = "/root/trampoline-test/source"
.\build-trampoline-qt\tupan.exe
```

## Testes

Configure o CMake com -DTEST_TRAMPOLINE=ON para habilitar o teste externo.
O backend Linux deve estar compilado. Os testes exercitam partida, desligamento,
periodicidade, três tipos de quadros inválidos, perda individual, interrupção
total, recuperação e reinicialização do backend. O modo local tem seu próprio
teste de regressão.

```powershell
cmake --build build-trampoline-qt
ctest --test-dir build-trampoline-qt --output-on-failure
```

Referência oficial: https://github.com/TrampolineRTOS/trampoline

Verificação da integração concluída em 7 de novembro de 2026. Os testes
simulator_can e simulator_trampoline passaram, com duração total de 9,81 s.

### Impressão dos quadros transmitidos

A tarefa Task_CAN_RX imprime uma linha TX para cada quadro enviado ao Qt:

```text
TX t=00000510 ms ID=0x100 DLC=2 DATA=[2C 07]
```

O Console Trampoline exibe essa linha recebida diretamente do backend.
O instante t pertence ao relógio do backend, em milissegundos.

### Acompanhar pelo terminal Linux

Com o painel aberto no modo Trampoline (WSL1), execute no PowerShell:

```powershell
wsl -d Ubuntu -u root -- tail -n 10 -F /tmp/painel-can.log
```

Dentro do Ubuntu, use apenas:

```bash
tail -n 10 -F /tmp/painel-can.log
```

O link painel-can.log aponta para a sessão mais recente. Cada processo
mantém seu próprio arquivo em /tmp, para preservar as linhas de sessões
simultâneas. Ctrl+C encerra o acompanhamento.

### Selecionar mensagens transmitidas

Na aba Monitor CAN, em Recepção, cada CAN-ID tem um checkbox na coluna
Enviar / CAN-ID. Marque para transmitir e desmarque para suspender aquele
quadro. É possível suspender vários IDs simultaneamente, inclusive 0x105.
As escolhas permanecem ao reconectar o backend ou trocar o modo de execução.
O seletor de suspensão individual também pode bloquear um sinal periódico.

No modo Trampoline, o Qt envia uma máscara de habilitação com seis bits no
comando STATE. Os bits 0 a 5 correspondem aos IDs 0x100 a 0x105. O backend
deixa de gerar os quadros desabilitados e retoma os periódicos ao reativar.
O quadro 0x105 transmite o estado atual dos comandos quando é reativado.
