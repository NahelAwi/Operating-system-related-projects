#include "Commands.h"
#include "signals.h"
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    LOGGING("smash started");
    if (signal(SIGTSTP, ctrlZHandler) == SIG_ERR) {
        perror("smash error: failed to set ctrl-Z handler");
    }
    if (signal(SIGINT, ctrlCHandler) == SIG_ERR) {
        perror("smash error: failed to set ctrl-C handler");
    }

    // TODO: setup sig alarm handler
    if (signal(SIGALRM, alarmHandler) == SIG_ERR) {
        perror("smash error: failed to set alarm handler");
    }

    SmallShell &smash = SmallShell::getInstance();
    while (!smash.quit) {
        std::cout << smash.display_prompt + "> ";
        std::string cmd_line;
        std::getline(std::cin, cmd_line);
        smash.executeCommand(cmd_line.c_str());
    }
    return 0;
}