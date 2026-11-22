#include "tpl_os.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

DeclareEvent(CanReady);
DeclareResource(VehicleResource);

static const unsigned periods[] = {500, 100, 200, 500, 100};
static const int minimums[] = {0, 0, 60, 0, 90};
static const int maximums[] = {2800, 100, 120, 100, 160};
static int input[] = {1800, 42, 88, 61, 138};
static int processed[] = {0, 0, 88, 61, 138};
static unsigned nowMs, startedAt, commands = 4;
static unsigned nextSend[5], runs[6], overflow;
static unsigned enabledMask = 63;
static int stopped, previousCommands = -1, quit, configured;
static int hot, reserve;
static int inputSocket = -1;
static FILE *canLog;

typedef struct {
    unsigned id, dlc, value, time;
} Frame;
static Frame queue[64];
static unsigned head, length;

static void enqueue(unsigned id, unsigned dlc, unsigned value) {
    if (length == 64) { ++overflow; return; }
    queue[(head + length) % 64] = (Frame){id, dlc, value, nowMs};
    ++length;
    SetEvent(Task_CAN_RX, CanReady);
}

static void command(const char *line) {
    int values[5], cmd, disconnect, mask;
    unsigned id, dlc, value;
    if (!strcmp(line, "QUIT")) { quit = 1; return; }
    if (sscanf(line, "STATE %d %d %d %d %d %d %d %d",
               &values[0], &values[1], &values[2], &values[3], &values[4],
               &cmd, &disconnect, &mask) == 8) {
        for (int i = 0; i < 5; ++i)
            if (values[i] < minimums[i] || values[i] > maximums[i]) return;
        if (cmd < 0 || cmd >= 64 || (cmd & 3) == 3 ||
            disconnect < 0 || disconnect > 1 || mask < 0 || mask > 63) return;
        if ((cmd & 32) && !(commands & 32)) startedAt = nowMs;
        if (stopped && !disconnect) previousCommands = -1;
        memcpy(input, values, sizeof input);
        commands = (unsigned)cmd;
        stopped = disconnect;
        enabledMask = (unsigned)mask;
        configured = 1;
    } else if (sscanf(line, "INJECT %u %u %u", &id, &dlc, &value) == 3) {
        if (id <= 0x7ff && dlc <= 8 && value <= 65535) enqueue(id, dlc, value);
    }
}

TASK(Task_Acquisition) {
    ++runs[1];
    nowMs += 10;
    char buffer[512];
    ssize_t n;
    while ((n = recv(inputSocket, buffer, sizeof buffer - 1, 0)) > 0) {
        buffer[n] = 0;
        buffer[strcspn(buffer, "\r\n")] = 0;
        command(buffer);
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) quit = 1;
    if (quit) { ShutdownOS(E_OK); TerminateTask(); return; }
    if (configured && !stopped) {
        GetResource(VehicleResource);
        for (int i = 0; i < 5; ++i) {
            if ((enabledMask & (1u << i)) && nowMs >= nextSend[i]) {
                enqueue(0x100 + (unsigned)i, 2, (unsigned)processed[i]);
                nextSend[i] = nowMs + periods[i];
            }
        }
        ReleaseResource(VehicleResource);
        if (!(enabledMask & 32)) previousCommands = -1;
        if ((enabledMask & 32) && (int)commands != previousCommands) {
            enqueue(0x105, 2, commands);
            previousCommands = (int)commands;
        }
    }
    TerminateTask();
}

TASK(Task_CAN_RX) {
    for (;;) {
        WaitEvent(CanReady);
        ClearEvent(CanReady);
        ++runs[0];
        while (length) {
            Frame frame = queue[head];
            head = (head + 1) % 64;
            --length;
            printf("CAN %u %u %u %u %u\n", frame.time, frame.id, frame.dlc,
                   frame.value & 255, frame.value >> 8);
            char text[160];
            int position = snprintf(text, sizeof text,
                                    "TX t=%08u ms ID=0x%03X DLC=%u DATA=[",
                                    frame.time, frame.id, frame.dlc);
            for (unsigned byte = 0; byte < frame.dlc; ++byte) {
                unsigned value = byte < 2 ? (frame.value >> (8 * byte)) & 255 : 0;
                position += snprintf(text + position, sizeof text - (size_t)position,
                                     "%s%02X", byte ? " " : "", value);
            }
            snprintf(text + position, sizeof text - (size_t)position, "]\n");
            fputs(text, stdout);
            if (canLog) fputs(text, canLog);
        }
        fflush(stdout);
    }
}

TASK(Task_Processing) {
    ++runs[2];
    GetResource(VehicleResource);
    memcpy(processed, input, sizeof processed);
    if (!(commands & 32)) { processed[0] = 0; processed[1] = 0; }
    else {
        double seconds = (nowMs - startedAt) / 1000.0;
        double wave = .7 * sin(2.3 * seconds) + .3 * sin(4.1 * seconds);
        for (int i = 0; i < 2; ++i) {
            if (input[i] == 0) continue;
            processed[i] += (int)lround((i == 0 ? 40 : 4) * wave);
            if (processed[i] < minimums[i]) processed[i] = minimums[i];
            if (processed[i] > maximums[i]) processed[i] = maximums[i];
        }
    }
    ReleaseResource(VehicleResource);
    TerminateTask();
}

TASK(Task_Display) {
    ++runs[3];
    GetResource(VehicleResource);
    printf("DISPLAY %u %d %d %d %d %d\n", nowMs, processed[0], processed[1],
           processed[2], processed[3], processed[4]);
    ReleaseResource(VehicleResource);
    fflush(stdout);
    TerminateTask();
}

TASK(Task_Diagnostics) {
    ++runs[4];
    GetResource(VehicleResource);
    hot = hot ? processed[2] >= 100 : processed[2] >= 105;
    reserve = reserve ? processed[3] <= 27 : processed[3] < 25;
    printf("DIAG %u %d %d\n", nowMs, hot, reserve);
    ReleaseResource(VehicleResource);
    fflush(stdout);
    TerminateTask();
}

TASK(Task_Logging) {
    ++runs[5];
    printf("TASKS %u %u %u %u %u %u %u %u\n", nowMs,
           runs[0], runs[1], runs[2], runs[3], runs[4], runs[5], overflow);
    fflush(stdout);
    TerminateTask();
}

int main(void) {
    inputSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (inputSocket < 0) return 1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(inputSocket, (struct sockaddr *)&address, sizeof address) < 0) return 1;
    socklen_t size = sizeof address;
    if (getsockname(inputSocket, (struct sockaddr *)&address, &size) < 0) return 1;
    int flags = fcntl(inputSocket, F_GETFL);
    if (flags < 0 || fcntl(inputSocket, F_SETFL, flags | O_NONBLOCK) < 0) return 1;
    char path[128], linkPath[128];
    snprintf(path, sizeof path, "/tmp/painel-can-%ld.log", (long)getpid());
    canLog = fopen(path, "w");
    if (canLog) {
        setvbuf(canLog, NULL, _IOLBF, 0);
        snprintf(linkPath, sizeof linkPath, "/tmp/painel-can-%ld.latest", (long)getpid());
        if (symlink(path, linkPath) == 0) {
            if (rename(linkPath, "/tmp/painel-can.log") < 0) perror("CAN log link");
        } else {
            perror("CAN log link");
        }
    } else {
        perror("CAN log");
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("READY TRAMPOLINE %u\n", ntohs(address.sin_port));
    StartOS(OSDEFAULTAPPMODE);
    return 0;
}
