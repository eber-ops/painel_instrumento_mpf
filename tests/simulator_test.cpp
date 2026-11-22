#include <QApplication>
#include <QCheckBox>
#include <QLabel>
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
        auto* rx = window.findChild<QTableWidget*>("receiveTable");
        if (!QTest::qWaitFor([&] { return rx->rowCount() == 6 && rx->item(5, 5)->text().toInt() > 0; }, 10000)) {
            qCritical() << "Backend did not deliver six CAN IDs";
            return false;
        }
        motorAndCycle();
        for (int fault = 0; fault < 3; ++fault) invalidFrames(fault);
        signalTimeoutAndRecovery();
        disconnectAndReconnect();
        selectMessages();
        if (qEnvironmentVariable("PANEL_BACKEND") == "trampoline") {
            auto* status = window.findChild<QLabel*>("engineStatus");
            check(QTest::qWaitFor([&] { return status->text().startsWith("Trampoline ativo"); }),
                  "Trampoline task counters not received");
            window.findChild<QPushButton*>("restartEngine")->click();
            check(QTest::qWaitFor([&] { return rx->rowCount() == 6 && rx->item(5, 5)->text().toInt() > 0; }, 10000),
                  "Backend did not reconnect");
            check(QTest::qWaitFor([&] { return status->text().startsWith("Trampoline ativo"); }),
                  "Task counters stopped after reconnect");
        }
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
        check(QTest::qWaitFor([&] { return rx->item(0, 5)->text().toInt() >= 2; }), "No second RPM frame");
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
        window.findChild<QComboBox*>("blockedSignal")->setCurrentIndex(1);
        QTest::qWait(80);
        const auto accepted = rx->item(0, 5)->text();
        choice->setCurrentIndex(fault);
        window.findChild<QPushButton*>("injectFault")->click();
        check(QTest::qWaitFor([&] {
            for (int row = 0; row < trace->rowCount(); ++row)
                if (trace->item(row, 5)->text() == choice->currentText()) return true;
            return false;
        }), "Injected frame did not reach Trace");
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

    void disconnectAndReconnect() {
        auto* disconnect = window.findChild<QCheckBox*>("disconnectCan");
        auto* rx = window.findChild<QTableWidget*>("receiveTable");
        disconnect->setChecked(true);
        check(QTest::qWaitFor([&] {
            for (int row = 0; row < 5; ++row)
                if (rx->item(row, 7)->text() != "Timeout") return false;
            return true;
        }), "Full CAN timeout was not detected");
        disconnect->setChecked(false);
        check(QTest::qWaitFor([&] {
            for (int row = 0; row < 5; ++row)
                if (rx->item(row, 7)->text() != "OK") return false;
            return true;
        }), "CAN did not recover after reconnect");
    }

    void selectMessages() {
        auto* rx = window.findChild<QTableWidget*>("receiveTable");
        rx->item(0, 0)->setCheckState(Qt::Unchecked);
        rx->item(1, 0)->setCheckState(Qt::Unchecked);
        rx->item(5, 0)->setCheckState(Qt::Unchecked);
        QTest::qWait(100);
        const auto rpmCount = rx->item(0, 5)->text();
        const auto torqueCount = rx->item(1, 5)->text();
        const auto commandsCount = rx->item(5, 5)->text();
        const auto batteryCount = rx->item(4, 5)->text().toInt();
        window.findChild<QPushButton*>("ignition")->click();
        QTest::qWait(650);
        check(rx->item(0, 5)->text() == rpmCount, "Unchecked RPM still transmitted");
        check(rx->item(1, 5)->text() == torqueCount, "Unchecked torque still transmitted");
        check(rx->item(5, 5)->text() == commandsCount, "Unchecked commands still transmitted");
        check(rx->item(4, 5)->text().toInt() > batteryCount, "Enabled battery stopped");
        rx->item(0, 0)->setCheckState(Qt::Checked);
        rx->item(1, 0)->setCheckState(Qt::Checked);
        rx->item(5, 0)->setCheckState(Qt::Checked);
        check(QTest::qWaitFor([&] { return rx->item(0, 5)->text() != rpmCount; }), "RPM did not resume");
        check(QTest::qWaitFor([&] { return rx->item(5, 5)->text() != commandsCount; }), "Commands did not resume");
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
