# Guia de implementação para Codex — Automotive Instrument Cluster com RTOS em C++

> **Objetivo:** implementar um projeto de graduação completo, reproduzível e demonstrável de painel de instrumentos automotivo com simulação determinística de conceitos OSEK/VDX, barramento CAN virtual, interface Qt 6, testes e documentação. Uma integração com o **Trampoline RTOS real** é uma extensão independente e não deve ser confundida com o simulador.
>
> **Instrução ao Codex:** leia este documento integralmente antes de editar arquivos. Execute por fases, mantendo o projeto compilável e os testes passando ao final de cada fase. Não declare uma funcionalidade concluída sem testes e evidências. Quando uma dependência externa não estiver disponível, implemente e teste o núcleo independente de Qt e registre claramente o bloqueio.

## 1. Resultado esperado

Entregar um aplicativo desktop em C++17 com:

1. **Painel automotivo**: velocímetro (0–250 km/h), conta-giros (0–8.000 RPM), combustível (0–100%), temperatura, setas, mensagens e luzes de alerta.
2. **Controles de simulação**: velocidade, aceleração/RPM, combustível, temperatura, setas, falhas CAN, carga de CPU e injeção de falhas.
3. **Motor RTOS simulado**: tempo virtual, tarefas, prioridades fixas, preempção, eventos, alarmes, recursos compartilhados e deadlines.
4. **CAN virtual**: quadros CAN clássicos de até 8 bytes, IDs definidos, codificação/decodificação, periodicidade, filas e tratamento de timeout.
5. **Monitor RTOS**: estados das tarefas, diagrama de Gantt/timeline, releases, dispatches, preempções, conclusões, misses, latências e utilização de CPU simulada.
6. **Diagnóstico e testes**: sobretemperatura, dados inválidos, timeout de mensagem, estouro de fila, perda de deadline e disputa por recursos.
7. **Relatórios**: exportação de logs, métricas e cenários para CSV/JSON.
8. **Documentação**: README, arquitetura, requisitos, manual de execução, roteiro de apresentação e relatório de validação.
9. **Extensão opcional**: exemplo separado com Trampoline RTOS real e GOIL/OIL em ambiente POSIX, se as dependências e a toolchain estiverem disponíveis.

**Critério geral:** a aplicação deve rodar sem placa, ECU, barramento CAN físico ou internet em tempo de execução.

## 2. Escopo e honestidade técnica

- O núcleo principal é um **simulador didático**, não um RTOS certificado nem uma implementação integral da norma OSEK/VDX/AUTOSAR.
- O tempo virtual mede o comportamento do modelo de escalonamento, não garante latência de tempo real do sistema operacional hospedeiro.
- `std::thread`, `std::mutex` e `sleep_for` podem ser usados na interface/integração, mas **não** devem ser confundidos com o escalonador virtual.
- O simulador deve usar **uma fonte única de tempo virtual**, com evolução determinística e reprodução de cenários por semente.
- O modo de integração com Trampoline real, quando implementado, deve ser identificado explicitamente como outro backend, com suas limitações e resultados separados.
- Não utilizar o sistema em veículos reais; trata-se de software acadêmico.

## 3. Stack e ambiente

- C++17, CMake >= 3.21, Ninja (preferencial), Qt 6 Widgets, GoogleTest, CTest.
- Linux como ambiente preferencial de desenvolvimento e CI; Windows também deve ser suportado para o simulador.
- Dependências Qt e GoogleTest documentadas. Preferir `find_package(Qt6 REQUIRED COMPONENTS Widgets)` e `find_package(GTest CONFIG QUIET)` com fallback de desenvolvimento explicitamente documentado.
- Nunca fazer download silencioso de dependências na compilação. Se usar FetchContent, documentar que exige rede e oferecer opção com pacote instalado.
- Aplicar `-Wall -Wextra -Wpedantic` (ou equivalentes MSVC) e sanitizers quando disponíveis.
- Não exigir hardware ou privilégios administrativos para executar os testes do núcleo.

## 4. Estrutura de diretórios

```text
AutomotiveCluster/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
├── .gitignore
├── .github/workflows/ci.yml
├── docs/
│   ├── requirements.md
│   ├── architecture.md
│   ├── scheduling.md
│   ├── can-protocol.md
│   ├── test-plan.md
│   ├── user-guide.md
│   ├── presentation-script.md
│   └── validation-report.md
├── config/
│   ├── tasks.json
│   └── scenarios/
│       ├── normal.json
│       ├── overheating.json
│       ├── can-timeout.json
│       ├── overload.json
│       └── resource-contention.json
├── include/cluster/
│   ├── core/{Clock,Task,Job,Scheduler,Event,Alarm,Resource,Trace}.hpp
│   ├── automotive/{VehicleData,CanFrame,CanCodec,CanBus,SensorModel,Diagnostics}.hpp
│   ├── app/{SimulationEngine,Scenario,Metrics,Export}.hpp
│   └── ui/{MainWindow,DashboardWidget,TaskMonitorWidget,TimelineWidget,ControlsWidget}.hpp
├── src/
│   ├── core/
│   ├── automotive/
│   ├── app/
│   ├── ui/
│   └── main.cpp
├── tests/
│   ├── core/
│   ├── automotive/
│   ├── app/
│   └── fixtures/
├── examples/
│   └── trampoline-posix/   # extensão opcional, isolada
└── assets/
    └── icons/              # apenas assets com licença apropriada
```

**Regra:** o núcleo `cluster_core` e os testes não devem depender de Qt. A UI deve consumir snapshots imutáveis e eventos do motor.

## 5. Arquitetura e fluxo de dados

```text
Controles do usuário / cenários JSON
                |
                v
       SensorModel / CAN virtual
                |
        fila de quadros CAN
                |
                v
      evento CAN_RX + scheduler
                |
                v
     aquisição -> processamento
                |
         ICM_Data (recurso)
                |
          +-----+------+
          |            |
      diagnóstico    display
          |            |
          +-----+------+
                |
         snapshot imutável
                |
       UI Qt + monitor + logs
```

A lógica do modelo de veículo deve ser separada da execução das tarefas. Uma mudança no slider altera a entrada do sensor; ela **não** atualiza diretamente o painel: a alteração percorre CAN/aquisição/processamento/display, permitindo observar latência e prioridades.

## 6. Modelo de dados

Definir tipos fortes e valores iniciais explícitos:

```cpp
struct VehicleData {
    double speedKmh{0.0};
    double rpm{800.0};
    double fuelPercent{80.0};
    double coolantCelsius{85.0};
    bool leftTurn{false};
    bool rightTurn{false};
    bool engineWarning{false};
    bool canTimeout{false};
    bool diagnosticFault{false};
    std::uint64_t timestampMs{0};
};
```

Definir `RawSensorState`, `DecodedCanState`, `VehicleData`, `DiagnosticState` e `DisplaySnapshot` separadamente para evitar mistura de responsabilidades. Os valores devem ser validados nos limites especificados. Manter timestamp e indicador de validade/frescura por sinal. Usar unidades no nome das variáveis.

## 7. Regras do escalonador virtual (obrigatórias)

### 7.1 Tempo e determinismo

- Tempo interno inteiro em **microssegundos** (`uint64_t`); exibição em ms.
- Tick de simulação configurável, inicial **1 ms**; permitir execução por eventos/intervalos menores em evolução futura.
- A simulação não deve depender de relógio de parede para decidir qual job executa.
- `runFor(durationUs)`, `step()`, `pause()`, `resume()` e `reset()` com semântica documentada.
- Ao usar animação em tempo real, o relógio de parede só controla a velocidade de reprodução; a ordem dos eventos depende do tempo virtual.

### 7.2 Tarefas e prioridades

Convenção: **menor número = maior prioridade**. Configuração inicial:

| Tarefa | Ativação | Prioridade | WCET simulado inicial | Deadline relativo |
|---|---|---:|---:|---:|
| `Task_CAN_RX` | Evento CAN | 1 | 0,5 ms | 5 ms |
| `Task_Acquisition` | 10 ms | 2 | 1 ms | 10 ms |
| `Task_Processing` | 20 ms | 3 | 2 ms | 20 ms |
| `Task_Display` | 50 ms | 4 | 2 ms | 50 ms |
| `Task_Diagnostics` | 100 ms | 5 | 3 ms | 100 ms |
| `Task_Logging` | 1000 ms | 6 | 5 ms | 1000 ms |

**Importante:** os custos são parâmetros **simulados**, não benchmarks de hardware. Com tick de 1 ms, representar custo de 0,5 ms exige resolução sub-ms ou arredondamento explicitamente documentado. Preferir agendamento em microssegundos para preservar esse custo.

- Cada ativação cria um **Job** distinto com `releaseTime`, `absoluteDeadline`, `remainingExecution`, `startTime`, `finishTime` e estado.
- Jobs prontos são selecionados por prioridade fixa; empate resolvido por ordem FIFO de liberação e ID estável.
- Permitir preempção quando um job de prioridade superior ficar pronto; preservar tempo restante do job interrompido.
- Detectar deadline perdido quando `now > absoluteDeadline` para job ainda não concluído; marcar uma única vez. Para conclusão, definir que `finishTime <= absoluteDeadline` atende o deadline.
- Jobs atrasados não desaparecem silenciosamente: política padrão é **continuar até concluir**, com métrica de atraso. Permitir política alternativa `drop` configurável, documentada e testada.
- Definir explicitamente o comportamento em múltiplas ativações da mesma tarefa: fila de jobs com limite configurável; contar `activationOverflow` ao exceder o limite.
- Estados de job: `READY`, `RUNNING`, `BLOCKED`, `COMPLETED`, `DROPPED`; estado de tarefa agregada: `SUSPENDED`, `READY`, `RUNNING`, `WAITING`. `WAITING` é extensão didática e deve ser descrita como tal.
- Registro de trace para release, dispatch, preemption, block, unblock, completion, deadline miss e drop.

### 7.3 Alarmes e eventos

- `Alarm` deve disparar uma ação periódica ou única em tempo virtual.
- `Event` deve permitir sinalizar/desbloquear tarefa de evento.
- CAN RX é ativada por chegada de quadro; evitar polling desnecessário.
- Testar evento antes/depois de espera, múltiplos sinais e alarmes simultâneos.

### 7.4 Recursos e sincronização

- Implementar `Resource` virtual com dono, fila de espera, aquisição/liberação e eventos de trace.
- Para demonstrar teto de prioridade, calcular e aplicar **prioridade efetiva** ao possuir recurso, restaurando-a na liberação. Descrever as diferenças em relação ao protocolo OSEK completo.
- Impedir liberação por job não proprietário; registrar erro controlado.
- Não usar `std::mutex` como substituto de simulação de prioridade teto.
- Separadamente, usar mutex/filas thread-safe reais apenas onde houver threads reais entre UI e worker.
- Implementar experimento de inversão de prioridade com protocolo habilitado/desabilitado e métricas comparáveis.

### 7.5 Métricas

Calcular por tarefa: ativações, execuções, misses, drops, tempo de resposta (mínimo/médio/máximo), espera, preempções e bloqueios. Calcular CPU virtual ocupada/ociosa e utilização no intervalo medido. Todas as métricas devem derivar do trace, sem valores fictícios.

## 8. Barramento CAN virtual

### 8.1 Frame e protocolo

```cpp
struct CanFrame {
    std::uint32_t id{0};
    std::uint8_t dlc{0};
    std::array<std::uint8_t, 8> data{};
    std::uint64_t timestampUs{0};
};
```

Usar CAN clássico de 11 bits, validar ID <= `0x7FF`, DLC <= 8 e documentar codificação **little-endian** para sinais multibyte neste protocolo didático. Não afirmar compatibilidade com DBC de fabricante.

| ID | Sinal | Formato proposto | Período |
|---|---|---|---|
| `0x100` | velocidade | uint16, 0,1 km/h/LSB | 100 ms |
| `0x101` | RPM | uint16, 1 RPM/LSB | 50 ms |
| `0x102` | temperatura | int16, 0,1 °C/LSB | 200 ms |
| `0x103` | combustível | uint8, 1%/LSB | 500 ms |
| `0x104` | setas/status | bit0 esquerda, bit1 direita, bit2 alerta | evento |

- Todos os frames do protocolo possuem DLC esperado por ID; rejeitar DLC inválido e ID desconhecido com diagnóstico.
- Simular fila CAN com capacidade configurável (padrão 64), ordem estável e política de overflow explícita (padrão descartar mais recente e contar erro).
- `CanCodec` codifica/decodifica sem Qt e sem dependências externas.
- Simular atraso de entrega, perda de quadros, duplicação opcional, corrupção de DLC e interrupção de tráfego.
- Timeout por sinal: inicialmente 3 vezes o período esperado, parametrizável. Ao expirar, sinalizar falha e manter último valor válido com marcação de stale (não apresentar como leitura fresca).
- Documentar diferença entre frequência de transmissão CAN (50–500 ms) e frequência das tarefas (10–100 ms).

### 8.2 Cenários

Normal, perda de velocidade, CAN parado, fila saturada, ID desconhecido, payload inválido, mensagens fora de ordem. Cada cenário deve ter resultado esperado e teste.

## 9. SensorModel e lógica automotiva

- Permitir modo **manual** (sliders) e **automático** (trajetória determinística com aceleração/desaceleração).
- Limites: velocidade 0–250 km/h, RPM 0–8000, combustível 0–100%, temperatura -40–150 °C.
- Regras de alerta configuráveis: sobretemperatura > 100 °C, combustível baixo < 15%, RPM excessivo > 6500, perda de CAN e dados inválidos.
- Implementar histerese opcional para temperatura (aciona acima de 100 °C, limpa abaixo de 95 °C) e documentá-la para evitar piscar.
- Setas com estado lógico e indicador visual piscando em intervalo configurável; o piscar deve ser governado por tempo virtual.
- Diagnósticos não devem modificar diretamente os sensores de entrada.
- Definir precedência de alertas e permitir múltiplos alertas simultâneos.

## 10. UI Qt 6 Widgets

### 10.1 Layout

Janela principal redimensionável, mínimo recomendado 1280×720, com três abas:

1. **Dashboard**: mostradores de velocidade e RPM, combustível, temperatura, setas, luzes e mensagens.
2. **RTOS Monitor**: tabela de tarefas, estado, prioridade, release, deadline, WCET, tempo de resposta, misses; timeline/Gantt com zoom e filtro por tarefa.
3. **Simulation Lab**: sliders de sensores, seleção de cenários, controles de execução, injeção de falhas, configurações e exportação.

Barra superior: `Iniciar`, `Pausar`, `Passo`, `Reiniciar`, velocidade 0,5×/1×/2×/10× e tempo virtual. Barra inferior: status CAN, misses e mensagens recentes.

### 10.2 Comportamento

- UI nunca chama diretamente funções internas do scheduler durante renderização.
- Engine publica snapshots imutáveis e traces via fila/sinais seguros.
- UI atualiza a no máximo ~20–30 FPS para evitar excesso de trabalho, mesmo quando o tempo virtual avança mais rapidamente.
- Usar `QPainter` para mostradores e timeline; permitir tema escuro, cores de alto contraste e texto legível.
- Não bloquear a thread principal em loops de simulação ou exportação.
- Implementar limite de itens do trace em memória e opção de exportar traces longos.
- A timeline deve representar dados reais do trace, incluindo idle e preempções, não uma animação pré-programada.
- Exibir alerta de dados stale e diferenciação entre **falha simulada** e **falha de execução do aplicativo**.

## 11. Configuração e persistência

Criar `config/tasks.json` com esquema versionado (`schemaVersion`) para tarefas, prioridades, períodos, deadlines, custos e limites. Criar cenários JSON com:

- nome, descrição e duração;
- valores iniciais e eventos programados (`atMs`, `action`, `payload`);
- seed opcional para aleatoriedade reproduzível;
- resultados esperados, quando aplicável.

Validar entrada: valores negativos, tarefas duplicadas, prioridades fora de faixa, período zero, DLC inválido e eventos fora de ordem devem resultar em mensagens claras, não crash. Não usar caminhos absolutos específicos do computador do desenvolvedor.

## 12. Logs e relatórios

- Trace em memória: `{timestampUs, eventType, taskId, jobId, details}`.
- Exportar `trace.csv`, `metrics.json` e `scenario-results.json` com cabeçalhos, unidades, versão de schema e seed.
- Garantir ordem determinística do trace para mesma configuração e seed.
- Criar resumo final: total de jobs, misses, taxa de misses, utilização, máximos de latência, perdas CAN e erros de recurso.
- Incluir botão de exportação e testes de round-trip de serialização.

## 13. Plano de testes (não negociável)

### 13.1 Unitários

1. **Scheduler:** prioridades, empate FIFO, preempção, retomada, idle, jobs sobrepostos, deadlines na fronteira exata, misses sem duplicidade, overflow de ativação.
2. **Alarm/Event:** disparo periódico, sinalização, espera, simultaneidade, reinicialização.
3. **Resource:** exclusão, proprietário inválido, bloqueio/desbloqueio, teto de prioridade, inversão controlada.
4. **CAN:** encode/decode, endianness, DLC, IDs, limites, overflow, timeout, perda e duplicação.
5. **Vehicle/Diagnostics:** limiares, histerese, stale, múltiplos alertas, setas.
6. **Scenario:** parser, entradas inválidas, seed e reprodutibilidade.
7. **Metrics/Export:** contagens, latência, CSV/JSON e consistência com trace.

### 13.2 Integração

- Slider -> sensor -> frame CAN -> tarefa CAN -> processamento -> snapshot -> dashboard.
- Sobretemperatura -> diagnóstico -> warning.
- Timeout CAN -> flag stale -> alerta.
- Carga artificial -> deadline miss visível no monitor e no relatório.
- Recurso contendido -> bloqueio e retomada corretos.

### 13.3 Reprodutibilidade

Executar cada cenário duas vezes e comparar trace normalizado e métricas. Criar teste de regressão com trace esperado pequeno e revisável. Não comparar timestamps de relógio de parede.

### 13.4 Interface

Testar inicialização sem crash, iniciar/pausar/passo/reset, alteração de slider, atualização de indicadores, mudança de abas e exportação. Se houver ambiente headless, usar Qt offscreen para smoke test quando suportado.

### 13.5 Critérios de aceitação mensuráveis

- Build limpa em Linux; build em Windows documentada e validada quando ambiente disponível.
- `ctest --output-on-failure` passa em todos os testes habilitados.
- Cenário normal: **zero deadline misses** com configuração inicial e carga nominal.
- Cenário overload: **ao menos um miss** causado pela carga configurada, com identificação do job.
- Cenário overheating: alerta acionado e limpo segundo histerese.
- Cenário can-timeout: alerta após limiar configurado; último valor marcado stale.
- Recurso: nenhuma posse simultânea; métricas demonstram diferença entre políticas.
- Reexecução com mesma seed: mesmo trace virtual e métricas.
- UI permanece responsiva durante simulação e exportação de duração razoável.
- Documentação permite reproduzir build, execução, testes e apresentação.

## 14. CMake e comandos esperados

O Codex deve fornecer `CMakePresets.json` e comandos equivalentes:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/automotive_cluster
```

O caminho final do executável pode variar conforme o gerador e o sistema operacional: documentar o caminho efetivo. Separar targets `cluster_core`, `cluster_app`, `cluster_tests`; `cluster_core` deve compilar sem Qt. Permitir `-DBUILD_GUI=OFF` para testar em servidores sem Qt.

## 15. CI e qualidade

- Workflow GitHub Actions Linux: configure, build core, testes; opcionalmente build GUI com dependências Qt.
- Adicionar clang-format com estilo consistente, clang-tidy opcional e sanitizers em build de desenvolvimento.
- Evitar `using namespace std;` em headers, variáveis globais mutáveis e `new/delete` manuais desnecessários.
- Preferir RAII, `std::unique_ptr`, `std::optional`, enums fortes e interfaces explícitas.
- Comentar regras de escalonamento e invariantes, não repetir trivialidades do código.
- Não introduzir dependência sem justificar e registrar a licença.

## 16. Integração opcional com Trampoline RTOS real

**Somente iniciar depois de concluir o simulador e seus testes.**

1. Criar `examples/trampoline-posix/README.md` com pré-requisitos e instruções verificadas para a versão do Trampoline escolhida.
2. Consultar repositório/documentação oficial do Trampoline e registrar commit/tag; não inventar comandos ou nomes de targets.
3. Instalar/configurar GOIL e a plataforma POSIX seguindo instruções oficiais da versão utilizada.
4. Criar arquivo OIL contendo OS, tarefas, prioridades, alarmes, eventos e recursos conforme sintaxe efetivamente suportada.
5. Gerar configuração e compilar exemplo mínimo; demonstrar tarefa periódica e evento.
6. Implementar tarefas de aquisição, processamento e diagnóstico com dados simulados, respeitando API real disponível.
7. Exportar logs do exemplo real e compará-los conceitualmente com o simulador; não assumir tempos idênticos.
8. Documentar limitações do port POSIX e não afirmar certificação ou garantia hard real-time.
9. Se não houver ambiente compatível, entregar apenas instruções verificáveis e marcar esta etapa como **não executada**, sem simular evidências.

## 17. Plano de execução obrigatório para o Codex

### Fase 0 — Preparação

**Implementar:** repositório, estrutura, CMake, README inicial, `.gitignore`, CI básico, projeto core vazio e teste smoke.

**Validar:** configuração, compilação e teste simples. Registrar versão das ferramentas e problemas encontrados.

### Fase 1 — Núcleo temporal e escalonador

**Implementar:** Clock virtual, Task/Job, releases, fila pronta, prioridade, preempção, deadline, trace e métricas básicas.

**Validar:** testes de prioridade, preempção, boundary deadline, idle e reprodutibilidade. Exemplo CLI de timeline.

### Fase 2 — Eventos, alarmes e recursos

**Implementar:** alarmes, eventos, espera, desbloqueio, recursos, política de teto de prioridade e traces.

**Validar:** testes de contenção, restauração de prioridade, bloqueios e eventos simultâneos.

### Fase 3 — CAN virtual

**Implementar:** CanFrame, CanCodec, fila, periodicidade, RX por evento, timeout e injeção de falhas.

**Validar:** codec, ordenação, overflow, timeout, IDs e DLC inválidos.

### Fase 4 — Veículo e diagnósticos

**Implementar:** SensorModel, dados compartilhados, tarefas de aquisição/processamento/display/diagnóstico/logging e cenários.

**Validar:** cenários CLI end-to-end, alertas, frescura de sinais e métricas.

### Fase 5 — Interface Qt

**Implementar:** MainWindow, DashboardWidget, controles, worker/snapshots, temas e execução.

**Validar:** aplicação responsiva e fluxo real do slider ao painel.

### Fase 6 — Monitor RTOS

**Implementar:** tabela de tarefas, timeline real, zoom, filtros, logs e estatísticas.

**Validar:** comparar timeline visual com trace exportado; teste de overload.

### Fase 7 — Exportação e robustez

**Implementar:** CSV/JSON, validação de configurações, tratamento de erros, limites de memória, smoke tests.

**Validar:** export/import, cenários inválidos, repetibilidade e build limpa.

### Fase 8 — Documentação e apresentação

**Implementar:** manuais, diagramas, relatório de testes, roteiro de demonstração de 10–15 minutos, capturas de tela reais e matriz requisito-teste.

**Validar:** seguir README em ambiente limpo e executar o roteiro completo.

### Fase 9 — Trampoline real (opcional)

**Implementar e validar:** conforme seção 16, isoladamente.

**Ao fim de cada fase, o Codex deve responder com:** arquivos criados/alterados, decisões técnicas, comandos executados, resultados de build/testes, limitações, pendências e próxima fase. **Não avançar se testes críticos falharem.**

## 18. Roteiro da demonstração acadêmica

1. **1 min — contexto:** explicar ICM, CAN, RTOS, prioridade e deadlines.
2. **2 min — arquitetura:** mostrar fluxo sensores -> CAN -> scheduler -> ICM_Data -> display.
3. **2 min — normal:** iniciar cenário, alterar velocidade e observar tarefas e painel.
4. **2 min — falhas automotivas:** temperatura alta, combustível baixo e CAN timeout.
5. **2 min — tempo real:** aumentar WCET, provocar miss, mostrar job/instante no Gantt e métricas.
6. **2 min — concorrência:** comparar recurso com/sem teto de prioridade e interpretar resultados.
7. **1 min — validação:** mostrar testes automatizados, exportação e relatório.
8. **1 min — conclusão:** limitações do simulador e, se concluída, comparação com Trampoline real.

Preparar cenário de fallback com execução CLI e relatórios caso a GUI falhe no dia.

## 19. Matriz resumida de rastreabilidade

| Requisito | Implementação | Evidência |
|---|---|---|
| Painel automotivo | DashboardWidget | Teste UI + captura |
| Tarefas periódicas | Scheduler/Alarm | Testes scheduler |
| Prioridades e preempção | Scheduler/Job | Trace + teste |
| CAN virtual | CanBus/CanCodec | Testes CAN |
| Sincronização | Resource | Teste de contenção |
| Alarmes e eventos | Alarm/Event | Testes core |
| Diagnósticos | Diagnostics | Cenários |
| Deadlines | Metrics/Trace | Cenário overload |
| Logging | Export | CSV/JSON |
| Trampoline real | Exemplo POSIX opcional | Build/log independente |

## 20. Definição de pronto (Definition of Done)

Uma fase está pronta somente quando: código compila, testes relacionados passam, APIs e comportamento estão documentados, não há falhas críticas conhecidas, logs/saídas permitem auditar o comportamento e o README foi atualizado. Se alguma verificação não foi executada, marcar **NÃO VERIFICADO** com motivo.

O projeto inteiro está pronto quando o usuário consegue clonar, compilar, rodar os cenários normal/sobreaquecimento/timeout/overload/contenção, observar o painel e o monitor RTOS, exportar evidências e reproduzir os testes usando apenas o README.

---

# Prompt inicial para colar no Codex

```text
Você é o engenheiro responsável por implementar o projeto descrito no arquivo
GUIA_CODEX_AUTOMOTIVE_CLUSTER.md. Leia o documento inteiro antes de iniciar.

Implemente o sistema incrementalmente, seguindo rigorosamente as fases 0 a 8.
Não implemente a fase 9 (Trampoline real) até que eu solicite.

Regras obrigatórias:
- C++17, CMake, núcleo independente de Qt, Qt 6 Widgets na interface.
- Simulação temporal determinística com jobs, preempção e deadlines corretos.
- CAN virtual, alarmes, eventos, recursos, diagnósticos e testes automatizados.
- Não usar uma interface fictícia nem timeline hardcoded: tudo deve derivar do motor.
- Manter o build funcionando a cada fase.
- Executar testes e relatar os resultados; nunca afirmar que testou sem executar.
- Se faltarem dependências, implementar o que for possível e documentar bloqueios.
- Não afirmar que o simulador é o Trampoline RTOS real.

Comece agora pela FASE 0. Ao terminar, apresente:
(1) resumo das alterações; (2) árvore de arquivos; (3) comandos executados;
(4) resultados dos testes; (5) pendências; (6) plano da próxima fase.
Aguarde minha aprovação antes de começar a fase seguinte.
```

## Prompts de continuidade

Para cada fase seguinte, use:

```text
Continue o projeto conforme GUIA_CODEX_AUTOMOTIVE_CLUSTER.md.
Implemente exclusivamente a FASE N, preservando os contratos das fases anteriores.
Antes de concluir, execute build e todos os testes disponíveis, atualize
README/documentação e informe evidências, limitações e próximos passos.
Não avance para a fase seguinte sem minha aprovação.
```
