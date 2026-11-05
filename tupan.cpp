#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFrame>
#include <QSaveFile>
#include <QTextStream>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace {
struct Frame {
    std::uint16_t id;
    std::uint8_t dlc;
    std::array<std::uint8_t, 8> data{};
};

struct Signal {
    std::uint16_t id;
    int min, max, initial, periodMs;
    const char* label;
};

constexpr Signal signalSpecs[]{
    {0x100, 0, 2800, 1800, 500, "RPM"},
    {0x101, 0, 100, 42, 100, "Torque (%)"},
    {0x102, 60, 120, 88, 200, "Temperatura (C)"},
    {0x103, 0, 100, 61, 500, "Combustivel (%)"},
    {0x104, 90, 160, 138, 100, "Bateria"}
};
constexpr int count = sizeof(signalSpecs) / sizeof(signalSpecs[0]);
std::array<int, count> received{};
int gear = 0;
unsigned flags = 0;
bool hot = false, lowFuel = false;


Frame encode(std::uint16_t id, unsigned value) {
    return {id, 2, {static_cast<std::uint8_t>(value),
                    static_cast<std::uint8_t>(value >> 8)}};
}

QString receive(const Frame& frame) {
    if (frame.dlc != 2) return QStringLiteral("DLC inválido");
    const unsigned value = frame.data[0] | (unsigned(frame.data[1]) << 8);
    for (int i = 0; i < count; ++i) {
        const auto& signal = signalSpecs[i];
        if (frame.id != signal.id) continue;
        if (value < unsigned(signal.min) || value > unsigned(signal.max)) return QStringLiteral("Valor fora da faixa");
        received[i] = int(value);
        if (i == 2) hot = hot ? value >= 100 : value >= 105;
        if (i == 3) lowFuel = lowFuel ? value <= 27 : value < 25;
        return {};
    }
    if (frame.id == 0x105) {
        if ((value & 3) >= 3 || value >= 64) return QStringLiteral("Valor fora da faixa");
        gear = value & 3;
        flags = value >> 2;
        return {};
    }
    return QStringLiteral("ID desconhecido");
}

class Dashboard : public QWidget {
public:
    bool stale = false;
    bool flash = false;
    std::array<bool, count> missing{};
    QStringList missingSignals;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto scale = qMin(width() / 920.0, height() / 380.0);
        p.translate((width() - 920 * scale) / 2, (height() - 380 * scale) / 2);
        p.scale(scale, scale);

        const QColor white("#edf4f5"), muted("#8ca0a9"), green("#a3e86d");
        const QColor amber("#ffcb69"), red("#ff7474"), cyan("#60d6e8");
        const QColor track("#27343d");
        auto text = [&](QRect box, const QString& value, int size, QColor color, bool bold = false,
                        Qt::Alignment alignment = Qt::AlignCenter) {
            auto font = p.font();
            font.setPixelSize(size);
            font.setBold(bold);
            p.setFont(font);
            p.setPen(color);
            p.drawText(box, alignment, value);
        };
        auto arc = [&](int x, int y, int radius, double fraction, QColor color, int thickness) {
            const QRect box(x - radius, y - radius, 2 * radius, 2 * radius);
            p.setPen(QPen(track, thickness, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(box, 225 * 16, -270 * 16);
            if (fraction > 0) {
                p.setPen(QPen(color, thickness, Qt::SolidLine, Qt::RoundCap));
                p.drawArc(box, 225 * 16, -qRound(270 * 16 * fraction));
            }
        };
        auto card = [&](QRect box) {
            p.setPen(QColor("#27343d"));
            p.setBrush(QColor("#141e26"));
            p.drawRoundedRect(box, 16, 16);
        };

        card({1, 1, 918, 378});
        text({24, 20, 300, 34}, QStringLiteral("PAINEL DE INSTRUMENTOS"), 18, white, true,
             Qt::AlignVCenter | Qt::AlignLeft);
        text({700, 20, 190, 34}, stale ? "CAN  /  TIMEOUT" : "CAN  /  SIMULADO",
             15, stale ? amber : green, true, Qt::AlignVCenter | Qt::AlignRight);
        text({25, 54, 265, 23}, stale ? "MOTOR SEM SINAL" : (flags & 8) ? "MOTOR LIGADO" : "MOTOR DESLIGADO", 13,
             !stale && (flags & 8) ? green : muted, true, Qt::AlignVCenter | Qt::AlignLeft);
        const bool blinking = !stale && (flags & 2) && flash;
        text({367, 22, 56, 40}, QStringLiteral("◀"), 23, blinking ? green : track);
        text({497, 22, 56, 40}, QStringLiteral("▶"), 23, blinking ? green : track);
        text({424, 22, 72, 40}, stale ? QStringLiteral("—") : QString(QChar("DNR"[gear])), 25, white, true);

        const QString unavailable = QStringLiteral("—");
        arc(460, 185, 100, missing[0] ? 0 : received[0] / 2800.0, green, 15);
        text({380, 142, 160, 70}, missing[0] ? unavailable : QString::number(received[0]), 54, white, true);
        text({390, 209, 140, 25}, "RPM", 14, muted, true);
        arc(198, 186, 67, missing[1] ? 0 : received[1] / 100.0, cyan, 10);
        text({139, 156, 118, 48}, missing[1] ? unavailable : QString::number(received[1]) + "%", 34, white, true);
        text({139, 205, 118, 24}, "TORQUE", 13, muted, true);

        auto gauge = [&](int x, int y, const QString& name, const QString& value,
                         double fraction, QColor normal, QColor warning, bool alert,
                         bool dangerAtStart, const QStringList& marks) {
            constexpr int radius = 54;
            const QRect dial(x - radius, y - radius, radius * 2, radius * 2);
            const auto point = [&](double angle, double distance) {
                const double radians = angle * 3.141592653589793 / 180.0;
                return QPointF(x + distance * std::cos(radians), y - distance * std::sin(radians));
            };
            p.setPen(QPen(track, 5, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(dial, 210 * 16, -240 * 16);
            p.setPen(QPen(normal, 4));
            p.drawArc(dial, (dangerAtStart ? 150 : 210) * 16, -180 * 16);
            p.setPen(QPen(warning, 4));
            p.drawArc(dial, (dangerAtStart ? 210 : 30) * 16, -60 * 16);

            for (int i = 0; i <= 8; ++i) {
                const double angle = 210 - 30 * i;
                p.setPen(QPen(muted, i % 2 == 0 ? 2 : 1));
                p.drawLine(point(angle, 43), point(angle, 49));
                if (i % 2 == 0) {
                    const auto position = point(angle, 32);
                    text({qRound(position.x()) - 12, qRound(position.y()) - 8, 24, 16},
                         marks[i / 2], 10, muted, true);
                }
            }
            const auto needle = alert ? warning : white;
            p.setPen(QPen(needle, 3, Qt::SolidLine, Qt::RoundCap));
            if (fraction >= 0) p.drawLine(QPointF(x, y), point(210 - 240 * fraction, 47));
            p.setPen(Qt::NoPen);
            p.setBrush(needle);
            p.drawEllipse(QPointF(x, y), 5, 5);
            text({x - 66, y + 32, 132, 24}, value, 17, needle, true);
            text({x - 66, y + 57, 132, 19}, name, 11, muted, true);
        };
        gauge(673, 170, "TEMPERATURA", missing[2] ? unavailable : QString::number(received[2]) + QStringLiteral(" °C"),
              missing[2] ? -1 : (received[2] - 60) / 60.0, cyan, red, hot && !missing[2], false, {"60", "75", "90", "105", "120"});
        gauge(819, 170, QStringLiteral("COMBUSTÍVEL"), missing[3] ? unavailable : QString::number(received[3]) + "%",
              missing[3] ? -1 : received[3] / 100.0, green, amber, lowFuel && !missing[3], true, {"E", "¼", "½", "¾", "F"});

        const int volts = received[4];
        const int consumption = received[0] == 0 ? 0 : 15 + received[0] * received[1] / 6000;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(!stale && (flags & 1) ? green : muted, 2, Qt::SolidLine, Qt::RoundCap));
        QPainterPath headlamp;
        headlamp.moveTo(91, 296);
        headlamp.cubicTo(106, 296, 106, 318, 91, 318);
        headlamp.closeSubpath();
        p.drawPath(headlamp);
        for (int y : {299, 305, 311})
            p.drawLine(86, y, 74, y + 5);

        p.setPen(QPen(!stale && (flags & 4) ? red : muted, 2, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(QPointF(219, 307), 10, 10);
        p.drawArc(QRect(203, 291, 32, 32), 143 * 16, 74 * 16);
        p.drawArc(QRect(203, 291, 32, 32), -37 * 16, 74 * 16);
        p.drawLine(219, 302, 219, 308);
        p.drawPoint(219, 312);
        text({285, 290, 164, 34}, "1234,5 h", 17, white, true);
        text({450, 290, 154, 34}, (missing[0] || missing[1]) ? unavailable : QString("%1,%2 l/h").arg(consumption / 10).arg(consumption % 10),
             17, white, true);
        text({605, 290, 154, 34}, missing[4] ? unavailable : QString("%1,%2 V").arg(volts / 10).arg(volts % 10), 17, white, true);
        text({760, 290, 136, 34}, stale ? unavailable : QString(gear == 1 ? "N" : gear == 0 ? "D2" : "R2"), 20, green, true);

        QString status = QStringLiteral("TELEMETRIA ATUALIZADA");
        QColor statusColor = muted;
        if (stale) {
            status = QStringLiteral("CAN INTERROMPIDO");
            statusColor = amber;
        } else if (!missingSignals.isEmpty()) {
            status = QStringLiteral("SEM SINAL: ") + missingSignals.join(", ");
            statusColor = amber;
        } else if (hot || lowFuel) {
            status = hot ? QStringLiteral("TEMPERATURA ALTA") : QStringLiteral("COMBUSTÍVEL NA RESERVA");
            if (hot && lowFuel) status += QStringLiteral("  •  COMBUSTÍVEL NA RESERVA");
            statusColor = hot ? red : amber;
        }
        text({20, 332, 880, 30}, status, 14, statusColor, true);
    }
};
}

int runSimulator(int argc, char* argv[], void (*onReady)(QWidget&)) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setStyleSheet(R"(
        QWidget { background: #0d141b; color: #edf4f5; font: 14px "Segoe UI"; }
        QFrame#controls { background: #141e26; border: 1px solid #27343d; border-radius: 16px; }
        QLabel, QCheckBox { background: transparent; }
        QLabel#heading { font-weight: bold; color: #a3e86d; }
        QTabWidget::pane { border: 0; }
        QTabBar::tab { background: #141e26; color: #8ca0a9; padding: 9px 18px; }
        QTabBar::tab:selected { background: #202d36; color: #a3e86d; }
        QTableWidget { background: #141e26; alternate-background-color: #18242d; border: 1px solid #27343d; }
        QHeaderView::section { background: #202d36; color: #edf4f5; padding: 6px; border: 0; }
        QPushButton#ignition { background: #a3e86d; color: #172417; border-radius: 7px; padding: 8px 12px; font-weight: bold; }
        QPushButton#ignition:checked { background: #27343d; color: #edf4f5; }
        QPushButton#ignition:focus { border: 2px solid #60d6e8; }
        QComboBox { background: #202d36; border: 1px solid #425562; border-radius: 6px; padding: 5px 9px; }
        QSlider::groove:horizontal { background: #334550; height: 4px; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #70bb51; border-radius: 2px; }
        QSlider::handle:horizontal { background: #a3e86d; width: 14px; margin: -5px 0; border-radius: 7px; }
    )");

    QWidget window;
    window.setWindowTitle("Simulador CAN");
    window.resize(990, 640);
    window.setMinimumSize(780, 550);
    auto* root = new QVBoxLayout(&window);
    auto* tabs = new QTabWidget;
    root->addWidget(tabs);
    auto* panel = new QWidget;
    tabs->addTab(panel, "Painel");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(14);
    auto* dashboard = new Dashboard;
    layout->addWidget(dashboard, 1);

    auto* controls = new QFrame;
    controls->setObjectName("controls");
    layout->addWidget(controls);
    auto* grid = new QGridLayout(controls);
    grid->setContentsMargins(18, 14, 18, 14);
    grid->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("SIMULAÇÃO DE SINAIS"));
    title->setObjectName("heading");
    grid->addWidget(title, 0, 0, 1, count);

    std::array<QSlider*, count> sliders{};
    for (int i = 0; i < count; ++i) {
        auto* label = new QLabel;
        auto* slider = new QSlider(Qt::Horizontal);
        sliders[i] = slider;
        slider->setRange(signalSpecs[i].min, signalSpecs[i].max);
        slider->setValue(signalSpecs[i].initial);
        slider->setAccessibleName(QString::fromLatin1(signalSpecs[i].label));
        auto updateLabel = [label, i](int value) {
            const auto display = i == 4 ? QString("%1,%2 V").arg(value / 10).arg(value % 10)
                                         : QString::number(value);
            label->setText(QString::fromLatin1(signalSpecs[i].label) + "  " + display);
        };
        updateLabel(slider->value());
        QObject::connect(slider, &QSlider::valueChanged, label, updateLabel);
        grid->addWidget(label, 1, i);
        grid->addWidget(slider, 2, i);
        grid->setColumnStretch(i, 1);
    }

    auto* direction = new QComboBox;
    direction->addItems({"Frente", "Neutro", QStringLiteral("Ré")});
    direction->setAccessibleName("Marcha");
    auto* lights = new QCheckBox("Farol");
    auto* hazard = new QCheckBox("Pisca-alerta");
    auto* brake = new QCheckBox("Freio");
    auto* disconnect = new QCheckBox("Interromper CAN");
    auto* ignition = new QPushButton("Ligar motor");
    ignition->setObjectName("ignition");
    ignition->setCheckable(true);
    QObject::connect(ignition, &QPushButton::toggled, ignition, [ignition](bool running) {
        ignition->setText(running ? "Desligar motor" : "Ligar motor");
    });
    lights->setChecked(true);
    grid->addWidget(direction, 3, 0);
    grid->addWidget(lights, 3, 1);
    grid->addWidget(hazard, 3, 2);
    grid->addWidget(brake, 3, 3);
    grid->addWidget(disconnect, 3, 4);
    grid->addWidget(ignition, 0, count - 1);

    auto* monitor = new QWidget;
    tabs->addTab(monitor, "Monitor CAN");
    auto* monitorLayout = new QVBoxLayout(monitor);
    auto* status = new QLabel(QStringLiteral("Aguardando quadros"));
    monitorLayout->addWidget(status);
    auto* faultBar = new QGridLayout;
    monitorLayout->addLayout(faultBar);
    auto* blockedSignal = new QComboBox;
    blockedSignal->setObjectName("blockedSignal");
    blockedSignal->addItem(QStringLiteral("Nenhum ID suspenso"), -1);
    for (int i = 0; i < count; ++i)
        blockedSignal->addItem(QString("0x%1 | %2").arg(signalSpecs[i].id, 3, 16)
                                   .arg(signalSpecs[i].label), i);
    auto* fault = new QComboBox;
    fault->setObjectName("faultType");
    fault->addItems({QStringLiteral("DLC inválido"), QStringLiteral("ID desconhecido"),
                     QStringLiteral("Valor fora da faixa")});
    auto* inject = new QPushButton(QStringLiteral("Injetar falha"));
    inject->setObjectName("injectFault");
    auto* exportTrace = new QPushButton(QStringLiteral("Exportar CSV"));
    faultBar->addWidget(blockedSignal, 0, 0);
    faultBar->addWidget(fault, 0, 1);
    faultBar->addWidget(inject, 0, 2);
    faultBar->addWidget(exportTrace, 0, 3);
    auto* views = new QTabWidget;
    monitorLayout->addWidget(views);
    auto* rx = new QTableWidget(0, 8);
    rx->setHorizontalHeaderLabels({"CAN-ID", "Tipo", "DLC", "Dados (hex)",
                                   "Ciclo (ms)", "Contagem", "Sinal", "Estado"});
    auto* frames = new QTableWidget(0, 6);
    frames->setHorizontalHeaderLabels({QStringLiteral("Tempo (ms)"), "ID", "DLC", "Dados (hex)", "Sinal", "Resultado"});
    rx->setObjectName("receiveTable");
    frames->setObjectName("traceTable");
    for (auto* table : {rx, frames}) {
        table->verticalHeader()->hide();
        table->verticalHeader()->setDefaultSectionSize(25);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setAlternatingRowColors(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
    }
    views->addTab(rx, QStringLiteral("Recepção"));
    views->addTab(frames, "Trace");

    struct Reception {
        int row;
        qint64 lastTime;
        quint64 count;
    };
    QHash<std::uint16_t, Reception> receptions;

    constexpr int historyLimit = 600;
    quint64 totalFrames = 0, rejectedFrames = 0;
    auto send = [&](const Frame& frame, qint64 now, const QString& signal) {
        const auto error = receive(frame);
        const auto* scroll = frames->verticalScrollBar();
        const bool follow = scroll->value() == scroll->maximum();
        if (frames->rowCount() == historyLimit) frames->removeRow(0);
        const int row = frames->rowCount();
        frames->insertRow(row);
        QStringList bytes;
        for (int i = 0; i < frame.dlc && i < int(frame.data.size()); ++i)
            bytes.append(QString("%1").arg(frame.data[i], 2, 16, QLatin1Char('0')).toUpper());
        const QStringList values{
            QString::number(now),
            "0x" + QString("%1").arg(frame.id, 3, 16, QLatin1Char('0')).toUpper(),
            QString::number(frame.dlc), bytes.join(' '), signal, error.isEmpty() ? QStringLiteral("Aceito") : error
        };
        for (int col = 0; col < values.size(); ++col)
            frames->setItem(row, col, new QTableWidgetItem(values[col]));
        if (follow) frames->scrollToBottom();
        if (!error.isEmpty()) {
            ++rejectedFrames;
            return;
        }
        auto entry = receptions.find(frame.id);
        if (entry == receptions.end()) {
            const int rxRow = rx->rowCount();
            rx->insertRow(rxRow);
            entry = receptions.insert(frame.id, {rxRow, now, 0});
            const QStringList initial{values[1], "STD", values[2], "", "", "", signal, "OK"};
            for (int col = 0; col < initial.size(); ++col)
                rx->setItem(rxRow, col, new QTableWidgetItem(initial[col]));
        }
        rx->item(entry->row, 2)->setText(values[2]);
        rx->item(entry->row, 3)->setText(values[3]);
        rx->item(entry->row, 4)->setText(entry->count ? QString::number(now - entry->lastTime)
                                                     : QStringLiteral("—"));
        rx->item(entry->row, 5)->setText(QString::number(++entry->count));
        entry->lastTime = now;
        ++totalFrames;
    };

    QElapsedTimer clock;
    clock.start();
    qint64 lastRx = 0;
    std::array<qint64, count> lastValid{};
    QObject::connect(inject, &QPushButton::clicked, &window, [&] {
        const int index = std::max(0, blockedSignal->currentData().toInt());
        const auto& spec = signalSpecs[index];
        auto frame = encode(spec.id, unsigned(spec.max + 1));
        if (fault->currentIndex() == 0) frame.dlc = 1;
        if (fault->currentIndex() == 1) frame.id = 0x7FF;
        send(frame, clock.elapsed(), QStringLiteral("Injeção manual"));
        views->setCurrentWidget(frames);
        frames->scrollToBottom();
    });
    QObject::connect(exportTrace, &QPushButton::clicked, &window, [&] {
        const auto path = QFileDialog::getSaveFileName(&window, QStringLiteral("Exportar Trace"),
                                                       "trace-can.csv", "CSV (*.csv)");
        if (path.isEmpty()) return;
        QSaveFile file(path);
        bool saved = file.open(QIODevice::WriteOnly | QIODevice::Text);
        if (saved) {
            QTextStream output(&file);
            output.setGenerateByteOrderMark(true);
            for (int col = 0; col < frames->columnCount(); ++col)
                output << (col ? ";" : "") << frames->horizontalHeaderItem(col)->text();
            output << '\n';
            for (int row = 0; row < frames->rowCount(); ++row) {
                for (int col = 0; col < frames->columnCount(); ++col) {
                    auto value = frames->item(row, col)->text();
                    output << (col ? ";" : "") << '"' << value.replace('"', "\"\"") << '"';
                }
                output << '\n';
            }
            output.flush();
            saved = output.status() == QTextStream::Ok && file.commit();
        }
        if (!saved) QMessageBox::warning(&window, QStringLiteral("Exportar Trace"),
                                         QStringLiteral("Não foi possível salvar o arquivo CSV."));
    });
    std::array<qint64, count> nextSend{};
    int lastCommands = -1;
    qint64 engineStartedAt = 0;
    QObject::connect(ignition, &QPushButton::toggled, &window, [&](bool running) {
        if (running) engineStartedAt = clock.elapsed();
    });
    auto tick = [&] {
        const auto now = clock.elapsed();
        if (!disconnect->isChecked()) {
            for (int i = 0; i < count; ++i) {
                if (i == blockedSignal->currentData().toInt() || now < nextSend[i]) continue;
                const auto& spec = signalSpecs[i];
                int value = sliders[i]->value();
                if (i < 2) {
                    if (!ignition->isChecked()) {
                        value = 0;
                    } else if (value > 0) {
                        const double seconds = (now - engineStartedAt) / 1000.0;
                        const double wave = 0.7 * std::sin(2.3 * seconds) + 0.3 * std::sin(4.1 * seconds);
                        const int amplitude = i == 0 ? 40 : 4;
                        value = std::clamp(value + qRound(amplitude * wave), spec.min, spec.max);
                    }
                }
                send(encode(spec.id, value), now, spec.label);
                nextSend[i] += ((now - nextSend[i]) / spec.periodMs + 1) * spec.periodMs;
                lastRx = lastValid[i] = now;
            }
            const int commands = direction->currentIndex() | (lights->isChecked() ? 4 : 0)
                               | (hazard->isChecked() ? 8 : 0) | (brake->isChecked() ? 16 : 0)
                               | (ignition->isChecked() ? 32 : 0);
            if (commands != lastCommands) {
                send(encode(0x105, unsigned(commands)), now, "Marcha e comandos");
                lastCommands = commands;
                lastRx = now;
            }
        }
        if (disconnect->isChecked()) lastCommands = -1;
        dashboard->stale = now - lastRx >= 700;
        dashboard->missingSignals.clear();
        for (int i = 0; i < count; ++i) {
            const auto& spec = signalSpecs[i];
            const bool missing = now - lastValid[i] >= 3 * spec.periodMs;
            dashboard->missing[i] = missing;
            if (missing) dashboard->missingSignals.append(QString::fromLatin1(spec.label));
            const auto entry = receptions.constFind(spec.id);
            if (entry != receptions.cend())
                rx->item(entry->row, 7)->setText(missing ? "Timeout" : "OK");
        }
        dashboard->flash = now / 400 % 2 == 0;
        if (tabs->currentWidget() == panel) dashboard->update();
        status->setText(QStringLiteral("%1  |  %2 aceitos  |  %3 rejeitados")
                            .arg(disconnect->isChecked() ? QStringLiteral("Envio interrompido")
                                                         : QStringLiteral("Recebendo"))
                            .arg(totalFrames).arg(rejectedFrames));
    };
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, tick);
    tick();
    timer.setTimerType(Qt::PreciseTimer);
    timer.start(10);
    window.show();
    if (onReady) onReady(window);
    return app.exec();
}
