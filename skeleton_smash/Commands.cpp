#include "Commands.h"
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
    #define FUNC_ENTRY() cout << __PRETTY_FUNCTION__ << " --> " << endl;

    #define FUNC_EXIT() cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
    #define FUNC_ENTRY()
    #define FUNC_EXIT()
#endif

string _ltrim(const std::string &s) {
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string &s) {
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string &s) {
    return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char *cmd_line, char **args) {
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;) {
        args[i] = (char *)malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;

    FUNC_EXIT()
}

bool _isBackgroundCommand(const char *cmd_line) {
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

// check if cmd_line has one of the spectators '|', '|&', '<', '<<' in it.
// in case it has, it store the seperator in 'sep' and the first index of the seperator in 'index' then return true.
// if return false, the 'sep','index' arguments not does not touche and their values are undefined.
bool _isSpecialCommand(const char *cmd_line, string &sep, size_t &index) {
    const string str(cmd_line);
    // check for pipe
    index = str.find_first_of('|');
    if (index != string::npos) {
        if (index + 1 < str.size() && str[index + 1] == '&')
            sep = "|&";
        else
            sep = "|";
        return true;
    }
    // check for redirection
    index = str.find_first_of('>');
    if (index != string::npos) {
        if (index + 1 < str.size() && str[index + 1] == '>')
            sep = ">>";
        else
            sep = ">";
        return true;
    }
    return false;
}

void _removeBackgroundSign(char *cmd_line) {
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

bool _hasSpecialCharacters(const char *cmd_line) {
    const string str(cmd_line);
    for (char c : SPECIAL_CHARACTERS)
        if (str.find(c) != string::npos)
            return true;
    return false;
}

inline bool _strToInt(const string &str, int &result) {
    try {
        result = std::stoi(str, NULL);
    } catch (std::out_of_range &) { return false; } catch (std::invalid_argument &) {
        return false;
    }
    return true;
}

// smash waitpid
pid_t swaitpid(pid_t pid, int *status, int options) {
    pid_t value = waitpid(pid, status, options);

    SmallShell &smash = SmallShell::getInstance();
    if (value != -1) {
        if (options == WUNTRACED && !WIFSTOPPED(*status)) {
            // remove from job list only if proccess is terminated (not stopped)
            // otherwise 'swaitpid' will be called again in 'removeAllFinishedJob
            smash.jobs_list.removeJobById(JobsList::jobId(*smash.jobs_list.foreground));
        }
        smash.jobs_list.foreground = NULL;
    } else
        PRINT_SYSCALL_FAIL("waitpid");
    return value;
}

// smash fork
pid_t sfork(Command &cmd) {
    pid_t pid = fork();
    if (pid == -1) {
        PRINT_SYSCALL_FAIL("fork");
    } else {
        SmallShell &smash = SmallShell::getInstance();
        // child process
        if (pid == 0) {
            setpgrp();
            if (cmd.background) {
                LOGGING("command " << cmd.args[0] << " started in background");
            }
        }
        // parent proccess
        else {
            smash.jobs_list.addJob(&cmd, pid);
            if (!cmd.background) {
                LOGGING("smash is waiting for proccess " << pid);
                int status = 0;
                swaitpid(pid, &status, WUNTRACED);
            }
        }
        return pid;
    }
}

// smash kill
int skill(pid_t pid, int sig) {
    int err = kill(pid, sig);
    if (err == -1)
        PRINT_SYSCALL_FAIL("kill");
    return err;
}

// smash getcwd
string sgetcwd() {
    string cwd;
    sgetcwd(cwd);
    return cwd;
}

int sgetcwd(string &cwd) {
    char buffer[PATH_MAX];
    if (getcwd(buffer, PATH_MAX) == NULL) {
        PRINT_SYSCALL_FAIL("getcwd");
        return errno;
    } else {
        cwd.assign(buffer);
        return 0;
    }
}

int sdup(int fd) {
    int err = dup(fd);
    if (err == -1)
        PRINT_SYSCALL_FAIL("dup2");
    return err;
}

int sdup2(int fd, int fd2) {
    int err = dup2(fd, fd2);
    if (err == -1)
        PRINT_SYSCALL_FAIL("dup2");
    return err;
}

int sclose(int fd) {
    int err = close(fd);
    if (err == -1)
        PRINT_SYSCALL_FAIL("close");
    return err;
}

int sopen(const char *__file, int __oflag, mode_t mode) {
    int err = open(__file, __oflag, mode);
    if (err == -1)
        PRINT_SYSCALL_FAIL("open");
    return err;
}

/* BASE CLASSES*/
Command::Command(const char *cmd_line)
    : original_cmd(cmd_line)
    , background(_isBackgroundCommand(cmd_line)) {
    char cmd[COMMAND_MAX_LENGTH + 1];
    cmd[COMMAND_MAX_LENGTH] = '\0';
    _removeBackgroundSign(strncpy(cmd, cmd_line, COMMAND_MAX_LENGTH));

    char *arguments[COMMAND_MAX_ARGS];
    int args_num = _parseCommandLine(cmd, arguments);
    for (int i = 0; i < args_num; ++i) {
        this->args.push_back(string(arguments[i]));
    }

    for (int i = 0; i < args_num; ++i)
        free(arguments[i]);
}

Command::~Command() {}

BuiltInCommand::BuiltInCommand(const char *cmd_line)
    : Command(cmd_line) {
    background = false;
}

ExternalCommand::ExternalCommand(const char *cmd_line)
    : Command(cmd_line) {
    if (_hasSpecialCharacters(cmd_line))
        special = true;
}

void ExternalCommand::execute() {
    pid_t pid = sfork(*this);
    if (pid == 0) {    // son
        if (special) { // run via bash
            string cmd;
            for (string s : args)
                cmd.append(s).append(" ");
            char *argv[] = {"/bin/bash", "-c", (const_cast<char *>(cmd.c_str())), NULL};
            execv(argv[0], argv);
            PRINT_SYSCALL_FAIL("execv");
        } else {                     // run internally
            char *argv[args.size()]; // size without args[0] but with 'NULL' in the end
            for (size_t i = 0; i < args.size(); ++i)
                argv[i] = (const_cast<char *>(args[i].c_str()));
            argv[args.size()] = NULL;
            execvp(args[0].c_str(), argv);
            PRINT_SYSCALL_FAIL("execvp");
        }
        LOGGING("execv fail");
        exit(1); // in case execv fail
    }
}

PipeCommand::PipeCommand(const char *cmd_line, int sep, bool ampersand)
    : Command(cmd_line)
    , sep(sep)
    , ampersand(ampersand) {
    background = false;
}

void PipeCommand::execute() {
    int mypipe[2];
    if (!pipe(mypipe)) {
        Command *cmd1, *cmd2;
        SmallShell &smash = SmallShell::getInstance();
        cmd1 = smash.CreateCommand(original_cmd.substr(0, sep).c_str());
        cmd2 =
            smash.CreateCommand(original_cmd.substr(sep + (ampersand ? 2 : 1), original_cmd.size() - sep - 1).c_str());
        cmd1->background = false;
        cmd2->background = false;

        pid_t pid2 = sfork(*cmd1); // cmd2
        if (pid2 == 0) {
            // child process in this case cmd1
            if (ampersand)
                sdup2(mypipe[1], STDERR_FILENO);
            else
                sdup2(mypipe[1], STDOUT_FILENO);

            close(mypipe[0]);
            sclose(mypipe[1]);

            if (cmd1 != NULL)
                cmd1->execute();
            exit(0); // in case it not external command
        } else {
            // parent process in this case cmd2
            int stdin_copy = sdup(STDIN_FILENO);
            sdup2(mypipe[0], STDIN_FILENO);
            sclose(mypipe[0]);
            sclose(mypipe[1]);

            if (cmd2 != NULL)
                cmd2->execute();

            sdup2(stdin_copy, STDIN_FILENO);
            sclose(stdin_copy);
        }
    } else {
        PRINT_SYSCALL_FAIL("pipe");
    }
}

RedirectionCommand::RedirectionCommand(const char *cmd_line, int sep, bool append)
    : Command(cmd_line)
    , sep(sep)
    , append(append) {}

void RedirectionCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    Command *cmd = smash.CreateCommand(original_cmd.substr(0, sep).c_str());
    cmd->background = false;
    string path = _trim(original_cmd.substr(sep + (append ? 2 : 1), original_cmd.size() - sep - 1));

    int file;
    if (append)
        file = sopen(path.c_str(), O_APPEND | O_WRONLY | O_CREAT, 0655);
    else
        file = sopen(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0655);
    if (file == -1)
        return; // in case file cannot be opened

    int stdout_copy = sdup(STDOUT_FILENO);
    sdup2(file, STDOUT_FILENO);

    if (cmd != NULL)
        cmd->execute();

    sclose(file);
    sdup2(stdout_copy, STDOUT_FILENO);
    sclose(stdout_copy);
}

/* BUILD-IN CLASESS*/
ChangePrompt::ChangePrompt(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void ChangePrompt::execute() {
    SmallShell &smash = SmallShell::getInstance();
    if (this->args.size() < 2)
        smash.display_prompt = DEFAULT_PROMPT;
    else
        smash.display_prompt = args[1];
}

ChangeDirCommand::ChangeDirCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void ChangeDirCommand::execute() {
    if (args.size() == 1) {
        return;
    }
    if (args.size() > 2) {
        PRINT_RED_ERROR("too many arguments");
        return;
    }

    SmallShell &smash = SmallShell::getInstance();
    string new_dir(args[1]);
    if (args[1] == "-") {
        if (smash.last_pwd == "") {
            PRINT_RED_ERROR("OLDPWD not set");
            return;
        } else
            new_dir = smash.last_pwd;
    }

    string cwd;
    if (sgetcwd(cwd) != 0)
        return;
    if (chdir(new_dir.c_str()) == 0) {
        smash.last_pwd = cwd;
        LOGGING("current directory successfully changed to " + sgetcwd());
    } else {
        PRINT_SYSCALL_FAIL("chdir");
    }
}

GetCurrDirCommand::GetCurrDirCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void GetCurrDirCommand::execute() {
    std::cout << sgetcwd() << "\n";
}

ShowPidCommand::ShowPidCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void ShowPidCommand::execute() {
    std::cout << "smash pid is " << getpid() << std::endl;
}

QuitCommand::QuitCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void QuitCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    smash.quit = true;
    if (args.size() > 1 && args[1] == ARG_KILL) {
        // quit message are handled in killAllJobs
        smash.jobs_list.killAllJobs();
    }
}

JobsCommand::JobsCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void JobsCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    smash.jobs_list.printJobsList();
}

ForegroundCommand::ForegroundCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void ForegroundCommand::execute() {
    int job_id = 0;
    SmallShell &smash = SmallShell::getInstance();
    JobsList::Job *job = NULL;

    if (args.size() == 2 && _strToInt(args[1], job_id)) {
        job = smash.jobs_list.getJobById(job_id);
        if (job == NULL) {
            PRINT_RED_ERROR("job-id " << job_id << " does not exist");
            return;
        }
    } else if (args.size() == 1) {
        job = smash.jobs_list.getLastJob();
        if (job == NULL) {
            PRINT_RED_ERROR("jobs list is empty");
            return;
        }
    } else {
        PRINT_RED_ERROR("invalid arguments");
        return;
    }

    PRINT_MSG(JobsList::jobEntry(*job).cmd.original_cmd << " : " << JobsList::jobEntry(*job).pid);
    if (kill(JobsList::jobEntry(*job).pid, SIGCONT) == 0) {
        JobsList::jobEntry(*job).state = Foreground;
        smash.jobs_list.foreground = job;
        int status = 0;
        swaitpid(JobsList::jobEntry(*job).pid, &status, WUNTRACED);
    } else {
        PRINT_SYSCALL_FAIL("kill");
    }
}

BackgroundCommand::BackgroundCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void BackgroundCommand::execute() {
    int job_id = 0;
    SmallShell &smash = SmallShell::getInstance();
    JobsList::Job *job = NULL;

    if (args.size() == 2 && _strToInt(args[1], job_id)) {
        job = smash.jobs_list.getJobById(job_id);
        if (job == NULL) {
            PRINT_RED_ERROR("job-id " << job_id << " does not exist");
            return;
        } else if (JobsList::jobEntry(*job).state == Background) {
            PRINT_RED_ERROR("job-id " << job_id << " is already running in the background");
            return;
        }
    } else if (args.size() == 1) {
        job = smash.jobs_list.getLastStoppedJob();
        if (job == NULL) {
            PRINT_RED_ERROR("there is no stopped jobs to resume");
            return;
        }
    } else {
        PRINT_RED_ERROR("invalid arguments");
        return;
    }

    PRINT_MSG(JobsList::jobEntry(*job).cmd.original_cmd << " : " << JobsList::jobEntry(*job).pid);
    if (kill(JobsList::jobEntry(*job).pid, SIGCONT) == 0) {
        time(&JobsList::jobEntry(*job).entry_time);
        JobsList::jobEntry(*job).state = Background;

    } else {
        PRINT_SYSCALL_FAIL("kill");
    }
}

TimeoutCommand::TimeoutCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void TimeoutCommand::execute() {}

FareCommand::FareCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void FareCommand::execute() {}

SetcoreCommand::SetcoreCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void SetcoreCommand::execute() {}

KillCommand::KillCommand(const char *cmd_line)
    : BuiltInCommand(cmd_line) {}

void KillCommand::execute() {
    int job_id, signal;
    SmallShell &smash = SmallShell::getInstance();
    if (args.size() == 3 && args[1][0] == '-' && _strToInt(args[1].substr(1), signal) && _strToInt(args[2], job_id)) {
        JobsList::Job *job = smash.jobs_list.getJobById(job_id);
        if (job == NULL) {
            PRINT_RED_ERROR("job-id " << job_id << " does not exist");
        } else {
            if (skill(JobsList::jobEntry(*job).pid, signal) == 0) {
                PRINT_MSG("signal number " << signal << " was sent to pid " << JobsList::jobEntry(*job).pid);
            }
        }
    } else {
        PRINT_RED_ERROR("invalid arguments");
    }
}

/*JOB ENTRY CLASS*/
JobsList::JobEntry::JobEntry(pid_t pid, jobState state, Command &cmd)
    : pid(pid)
    , state(state)
    , cmd(cmd) {
    time(&entry_time);
}
JobsList::JobEntry::~JobEntry() {}

void JobsList::JobEntry::printJob(int job_id) {
    PRINT_MSG("[" << job_id << "] " << cmd.original_cmd << " : " << pid << " " << (int)difftime(time(NULL), entry_time)
                  << " secs" << ((state == Stopped) ? " (stopped)" : ""));
}

/* JOB LIST CLASS */
JobsList::JobsList() {}

JobsList::~JobsList() {}

void JobsList::addJob(Command *cmd, pid_t pid, bool is_stopped) {
    removeFinishedJobs();

    jobState state;
    if (is_stopped)
        state = Stopped;
    else if (cmd->background)
        state = Background;
    else
        state = Foreground;

    int new_job_id = (jobs.empty()) ? 1 : jobId(*jobs.rbegin()) + 1;
    pair<JobsMap::iterator, bool> result = jobs.insert(Job(new_job_id, JobEntry(pid, state, *cmd)));
    if (result.second) {
        if (state == Foreground)
            foreground = &*result.first;
        LOGGING("job " << jobId(*result.first) << ", pid=" << pid << " added to jobs list"
                       << ((state == Foreground) ? " (foreground)" : ""));
    } else
        LOGGING("ERROR adding job to jobs list fail");
}

void JobsList::printJobsList() {
    removeFinishedJobs();
    for (auto &job : jobs) {
        if (jobEntry(job).state != Foreground)
            jobEntry(job).printJob(jobId(job));
    }
}

void JobsList::killAllJobs() {
    int jobs_number = removeFinishedJobs();
    PRINT_SMASH("sending SIGKILL signal to " << jobs_number << " jobs:");
    for (JobsMap::iterator it = jobs.begin(); it != jobs.end();) {
        PRINT_MSG(jobEntry(*it).pid << ": " << jobEntry(*it).cmd.original_cmd);
        if (skill(jobEntry(*it).pid, SIGKILL) == 0) {
            jobs.erase(jobId(*it++));
            continue;
        } else {
            if (errno == ESRCH) { // pid cannot be found
                LOGGING("WARNING job " << jobId(*it) << "kill fail since its pid cannot be found");
                jobs.erase(jobId(*it++));
                continue;
            }
        }
        ++it;
    }
}

int JobsList::removeFinishedJobs() {
    int state = 0;
    for (JobsMap::iterator it = jobs.begin(); it != jobs.end();) {
        if (swaitpid(jobEntry(*it).pid, &state, WNOHANG) != 0)
            jobs.erase(jobId(*it++));
        else
            ++it;
    }
    return jobs.size();
}

JobsList::Job *JobsList::getJobById(int job_id) {
    removeFinishedJobs();
    JobsMap::iterator it = jobs.find(job_id);
    if (it != jobs.end())
        return &*it;
    else {
        LOGGING("job id " << job_id << " not in job list");
        return NULL;
    }
}

void JobsList::removeJobById(int job_id) {
    jobs.erase(job_id);
}

JobsList::Job *JobsList::getLastJob() {
    removeFinishedJobs();
    if (jobs.empty())
        return NULL;
    else
        return &*jobs.rbegin();
}

JobsList::Job *JobsList::getLastStoppedJob() {
    removeFinishedJobs();
    for (JobsMap::reverse_iterator rit = jobs.rbegin(); rit != jobs.rend(); ++rit) {
        if (jobEntry(*rit).state == Stopped)
            return &*rit;
    }
    return NULL;
}

/* SMALL SHALL CLASS */
SmallShell::SmallShell()
    : display_prompt(DEFAULT_PROMPT) {}

SmallShell::~SmallShell() {}

/**
 * Creates and returns a pointer to Command class which matches the given command line (cmd_line)
 */
Command *SmallShell::CreateCommand(const char *cmd_line) {
    // CHECK FOR REDIRECTION OR PIPE BEFORE CHECKING FIRST WORD
    size_t index = 0;
    string sep;
    if (_isSpecialCommand(cmd_line, sep, index)) {
        if (sep.compare("|") == 0)
            return new PipeCommand(cmd_line, index, false);
        else if (sep.compare("|&") == 0)
            return new PipeCommand(cmd_line, index, true);
        else if (sep.compare(">") == 0)
            return new RedirectionCommand(cmd_line, index, false);
        else if (sep.compare(">>") == 0)
            return new RedirectionCommand(cmd_line, index, true);
    } else {
        string cmd_s = _trim(string(cmd_line));
        if (cmd_s.empty())
            return NULL;
        string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

        if (firstWord.compare("chprompt") == 0)
            return new ChangePrompt(cmd_line);
        else if (firstWord.compare("pwd") == 0)
            return new GetCurrDirCommand(cmd_line);
        else if (firstWord.compare("showpid") == 0)
            return new ShowPidCommand(cmd_line);
        else if (firstWord.compare("cd") == 0)
            return new ChangeDirCommand(cmd_line);
        else if (firstWord.compare("fg") == 0)
            return new ForegroundCommand(cmd_line);
        else if (firstWord.compare("bg") == 0)
            return new BackgroundCommand(cmd_line);
        else if (firstWord.compare("jobs") == 0)
            return new JobsCommand(cmd_line);
        else if (firstWord.compare("kill") == 0)
            return new KillCommand(cmd_line);
        else if (firstWord.compare("quit") == 0)
            return new QuitCommand(cmd_line);
        else
            return new ExternalCommand(cmd_line);
    }
    return nullptr;
}

void SmallShell::executeCommand(const char *cmd_line) {
    Command *cmd = CreateCommand(cmd_line);
    if (cmd != NULL)
        cmd->execute();
}
