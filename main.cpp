class QWidget;

int runSimulator(int argc, char* argv[], void (*onReady)(QWidget&));

int main(int argc, char* argv[]) {
    return runSimulator(argc, argv, nullptr);
}
