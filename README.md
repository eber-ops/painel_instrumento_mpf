# Simulador CAN

Aplicativo C++17 com uma única tela em Qt 6 Widgets, inspirada no painel inicial
do Tupan. Os mostradores são desenhados com QPainter e acompanham o tamanho
da janela.

## Compilar no Windows

Abra um novo PowerShell com CMake e Ninja no PATH. O exemplo usa
Qt 6.8.3 e MinGW 13.1 instalados no perfil do usuário. Ajuste o caminho se o Qt
estiver em outro local.

```powershell
$qt = "$env:LOCALAPPDATA/TupanQt/6.8.3/mingw_64"
$mingw = "$env:LOCALAPPDATA/TupanQt/Tools/mingw1310_64/bin"
$env:Path = "$mingw;$env:Path"
cmake -S . -B build-qt6 -G Ninja "-DCMAKE_CXX_COMPILER=$mingw/g++.exe" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt"
cmake --build build-qt6
& "$qt/bin/windeployqt.exe" --release --no-translations .\build-qt6\tupan.exe
.\build-qt6\tupan.exe
```

## Simulação

Os controles inferiores representam os sensores e comandos do veículo. O
transmissor envia os quadros ao receptor interno conforme a tabela abaixo.
O painel usa os valores decodificados. Cada quadro tem ID de 11 bits e dois
bytes de dados em little-endian. Os IDs e formatos são didáticos.

| ID | Dado | Ciclo |
| --- | --- | --- |
| 0x100 | Rotação em RPM | 500 ms |
| 0x101 | Torque em % | 100 ms |
| 0x102 | Temperatura em °C | 200 ms |
| 0x103 | Nível de combustível em % | 500 ms |
| 0x104 | Tensão da bateria em décimos de volt | 100 ms |
| 0x105 | Marcha nos bits 0–1 (0=D, 1=N, 2=R); farol, pisca-alerta e freio nos bits 2–4; motor ligado no bit 5 | Inicialização, reconexão e mudança |

O ciclo de rotação segue a página 5 do enunciado. O ciclo
exibido no Monitor CAN é o intervalo medido entre recepções e pode variar
com a temporização do Windows.

O horímetro tem valor ilustrativo fixo. O consumo é estimado a partir da rotação
e do torque recebidos.

- Clique em **Ligar motor**. Com o motor ligado, RPM e torque oscilam suavemente em torno dos valores dos controles, com variação de até 40 RPM e 4 pontos percentuais, respectivamente. Ao desligar, ambos passam a zero.
- Suba a temperatura até 105 °C e reduza abaixo de 100 °C para ver a histerese.
- Baixe o combustível abaixo de 25% e suba acima de 27% para testar a reserva.
- Marque **Interromper CAN**. Após cerca de 700 ms, o painel sinaliza timeout e
  marca cada leitura como indisponível após três ciclos do respectivo sinal. Desmarque para retomar a atualização.





A aba **Monitor CAN** tem duas visualizações. **Recepção** mostra uma linha por
ID, com o último payload em hexadecimal, o DLC, o intervalo entre as duas
últimas recepções e a contagem acumulada. **Trace** mantém os últimos 600
quadros em ordem de chegada com seus tempos relativos à abertura do aplicativo.

### Diagnóstico e evidências

- No **Monitor CAN**, selecione um ID periódico para suspender apenas aquele
  sinal. Após três ciclos sem quadro válido, a lista de recepção mostra
  **Timeout** e o painel apresenta **—** para o valor indisponível. Volte a
  **Nenhum ID suspenso** para recuperar a leitura.
- Escolha um tipo de erro e clique em **Injetar falha**. O Trace mostra o motivo
  da rejeição. A lista de recepção contabiliza apenas quadros válidos.
- Use **Exportar CSV** para salvar os últimos 600 registros do Trace, incluindo
  quadros rejeitados. O arquivo usa UTF-8 e ponto e vírgula como separador.


A verificação automatizada em Qt offscreen passou para partida e desligamento,
ciclo de RPM de aproximadamente 500 ms, alertas de temperatura e reserva,
rejeição dos três tipos de quadro inválido e timeout e recuperação do RPM
com o torque ainda disponível. A gravação pelo diálogo de exportação CSV
ainda precisa de verificação manual.

### Executar a suíte CTest

Com a configuração de build acima e o Qt Test instalado, execute:

```powershell
$env:Path = "$qt/bin;$mingw;$env:Path"
cmake --build build-qt6
ctest --test-dir build-qt6 --output-on-failure
```

O teste de integração usa a janela e o temporizador reais com a plataforma
Qt offscreen. Ele verifica partida e desligamento, intervalo de recepção do
RPM, rejeição de DLC inválido, ID desconhecido e valor fora da faixa, além
de perda e recuperação do RPM com o torque ainda ativo. Configure com
`-DBUILD_TESTING=OFF` para compilar apenas o aplicativo.
