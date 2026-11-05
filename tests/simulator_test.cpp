#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QDebug>
#include <QTimer>

int runSimulator(int argc, char* argv[], void (*onReady)(QWidget&));

class SimulatorTest {
public:
    explicit SimulatorTest(QWidget& window) : window(window) {}

    bool run() {
        motorAndCycle();
        for (int fault = 0; fault < 3; ++fault) invalidFrames(fault);
        signalTimeoutAndRecovery();
        return passed;
    }

private:
    void motorAndCycle() {
        auto* rx = window.findChild<QTableWidget*>("receiveTable");
        auto* motor = window.findChild<QPushButton*>("ignition");
        check((rx->item(0, 0)->text()) == (QString("0x100")), "rx->item(0, 0)->text()");
        check((rx->item(0, 3)->text()) == (QString("00 00")), "rx->item(0, 3)->text()");
        check((rx->item(1, 3)->text()) == (QString("00 00")), "rx->item(1, 3)->text()");
        motor->click();
        check(QTest::qWaitFor([&] { return rx->item(0, 3)->text() != "00 00"; }), "Motor signal did not update");
        check(QTest::qWaitFor([&] { return rx->item(1, 3)->text() != "00 00"; }), "Motor signal did not update");
        const auto interval = rx->item(0, 4)->text().toInt();
        check(qAbs(interval - 500) < 100, "RPM interval outside tolerance");
        motor->click();
        check(QTest::qWaitFor([&] { return (rx->item(0, 3)->text()) == (QString("00 00")); }), "Timeout waiting for rx->item(0, 3)->text()");
        check(QTest::qWaitFor([&] { return (rx->item(1, 3)->text()) == (QString("00 00")); }), "Timeout waiting for rx->item(1, 3)->text()");
    }

    void invalidFrames(int fault) {
        auto* choice = window.findChild<QComboBox*>("faultType");
        auto* trace = window.findChild<QTableWidget*>("traceTable");
        auto* rx = window.findChild<QTableWidget*>("receiveTable");
        const auto accepted = rx->item(0, 5)->text();
        choice->setCurrentIndex(fault);
        window.findChild<QPushButton*>("injectFault")->click();
        check((trace->item(trace->rowCount() - 1, 5)->text()) == (choice->currentText()), "trace->item(trace->rowCount() - 1, 5)->text()");
        check((rx->item(0, 5)->text()) == (accepted), "rx->item(0, 5)->text()");
        check(rx->rowCount() == 6, "Rejected frame appeared in reception list");
    }

    void signalTimeoutAndRecovery() {
        auto* blocked = window.findChild<QComboBox*>("blockedSignal");
        auto* rx = window.findChild<QTableWidget*>("receiveTable");
        const auto torqueCount = rx->item(1, 5)->text().toInt();
        blocked->setCurrentIndex(1);
        check(QTest::qWaitFor([&] { return (rx->item(0, 7)->text()) == (QString("Timeout")); }), "Timeout waiting for rx->item(0, 7)->text()");
        check((rx->item(1, 7)->text()) == (QString("OK")), "rx->item(1, 7)->text()");
        check(rx->item(1, 5)->text().toInt() > torqueCount, "Torque stopped during RPM timeout");
        blocked->setCurrentIndex(0);
        check(QTest::qWaitFor([&] { return (rx->item(0, 7)->text()) == (QString("OK")); }), "Timeout waiting for rx->item(0, 7)->text()");
    }

    void check(bool ok, const char* message) {
        if (!ok) qCritical() << message;
        passed &= ok;
    }

    bool passed = true;
    QWidget& window;
};

int main(int argc, char* argv[]) {
    return runSimulator(argc, argv, [](QWidget& window) {
        QTimer::singleShot(0, &window, [&window] {
            SimulatorTest test(window);
            QCoreApplication::exit(test.run() ? 0 : 1);
        });
    });
}
