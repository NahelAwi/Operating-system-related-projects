#include "signals.h"
#include "Commands.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

#define MSG_CTRL_C "got ctrl-C"
#define MSG_CTRL_Z "got ctrl-Z"
#define MSG_ALARM "got an alarm"

using namespace std;

void ctrlZHandler(int sig_num) {
    PRINT_SMASH(MSG_CTRL_Z);
    LOGGING("received signal SIGTSTP");
    SmallShell &smash = SmallShell::getInstance();
    JobsList::Job *foreground = smash.jobs_list.foreground;
    if (foreground != NULL) {
        JobsList::JobEntry &job = JobsList::jobEntry(*foreground);
        if (skill(job.pid, SIGSTOP) == 0) {
            PRINT_PROCESS_STOPPED(job.pid);
            job.state = Stopped;
        }
    } else
        LOGGING("there is no foreground job");
}

void ctrlCHandler(int sig_num) {
    PRINT_SMASH(MSG_CTRL_C);
    LOGGING("received signal SIGINT");
    SmallShell &smash = SmallShell::getInstance();
    JobsList::Job *foreground = smash.jobs_list.foreground;
    if (foreground != NULL) {
        pid_t pid = JobsList::jobEntry(*foreground).pid;
        if (skill(pid, SIGKILL) == 0) {
            PRINT_PROCESS_KILLED(pid);
        }
    } else
        LOGGING("there is no foreground job");
}

void alarmHandler(int sig_num) {
    PRINT_SMASH(MSG_ALARM);
    LOGGING("received signal SIGALRM");
}
