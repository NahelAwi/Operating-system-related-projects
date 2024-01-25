#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_

#include <fcntl.h>
#include <map>
#include <string>
#include <time.h>
#include <vector>

using namespace std;

#define _DEBUG_XXX
#ifdef DEBUG_XXX
    #define LOGGING(msg) (std::cout << "log [proccess " << getpid() << "]: " << msg << std::endl)
#else
    #define LOGGING(msg)
#endif

#define MSG_SMASH "smash: "
#define MSG_SMASH_ERR "smash error: "

#define PRINT_MSG(msg) (std::cout << msg << std::endl)
#define PRINT_SMASH(msg) (std::cout << MSG_SMASH << msg << std::endl)
#define PRINT_PROCESS_KILLED(foreground_pid)                                                                           \
    (std::cout << MSG_SMASH << "process " << foreground_pid << " was killed" << std::endl)
#define PRINT_PROCESS_STOPPED(foreground_pid)                                                                          \
    (std::cout << MSG_SMASH << "process " << foreground_pid << " was stopped" << std::endl)
#define PRINT_PROCCESS_TIMEOUT(cmd) (std::cout << MSG_SMASH << cmd << " timed out!" << std::endl)

#define PRINT_RED_ERROR(err)                                                                                           \
    (std::cerr << MSG_SMASH_ERR << args[0] << ": " << err << std::endl) // for errors marks in red
#define PRINT_ERROR(err) (std::cout << MSG_SMASH_ERR << err << std::endl)
#define PRINT_SYSCALL_FAIL(syscall_name) (perror((MSG_SMASH_ERR + string(syscall_name) + " failed").c_str()))

#define COMMAND_MAX_LENGTH (80)
#define COMMAND_MAX_ARGS (20) // includes command
#define MAX_PROCESSES (100)
#define PROCESS_MAX_NAME_LENGTH (50)
#define DEFAULT_PROMPT "smash"
#define ARG_KILL "kill"
#define SPECIAL_CHARACTERS (std::vector<char>({'*', '?'}))

// smash specials
string sgetcwd();
int sgetcwd(string &cwd);
int skill(pid_t pid, int sig);

// Foreground: running at foreground - not in jobs list
// Background: running at background - in jobs list
// Stopped: stopped and moved background - in jobs list
enum jobState { Foreground, Background, Stopped }; //, Finished, Running };

class Command {
  public:
    std::vector<string> args; // FIGURE does first argument is the command?
    string original_cmd;
    bool background;

  public:
    Command(const char *cmd_line);
    virtual ~Command();
    virtual void execute() = 0;
    // virtual void prepare();
    // virtual void cleanup();
};

class BuiltInCommand : public Command {
  public:
    BuiltInCommand(const char *cmd_line);
    virtual ~BuiltInCommand() {}
};

class ExternalCommand : public Command {
  public:
    bool special = false;

  public:
    ExternalCommand(const char *cmd_line);
    virtual ~ExternalCommand() {}
    void execute() override;
};

class PipeCommand : public Command {
  public:
    int sep;
    bool ampersand;
    PipeCommand(const char *cmd_line, int sep, bool ampersand);
    virtual ~PipeCommand() {}
    void execute() override;
};

class RedirectionCommand : public Command {
  public:
    int sep;
    bool append;
    explicit RedirectionCommand(const char *cmd_line, int sep, bool append);
    virtual ~RedirectionCommand() {}
    void execute() override;
    // void prepare() override;
    // void cleanup() override;
};

class ChangePrompt : public BuiltInCommand {
  public:
    ChangePrompt(const char *cmd_line);
    virtual ~ChangePrompt() {}
    void execute() override;
};

class ChangeDirCommand : public BuiltInCommand {
  public:
    ChangeDirCommand(const char *cmd_line);
    virtual ~ChangeDirCommand() {}
    void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand {
  public:
    GetCurrDirCommand(const char *cmd_line);
    virtual ~GetCurrDirCommand() {}
    void execute() override;
};

class ShowPidCommand : public BuiltInCommand {
  public:
    ShowPidCommand(const char *cmd_line);
    virtual ~ShowPidCommand() {}
    void execute() override;
};

class JobsList;
class QuitCommand : public BuiltInCommand {
  public:
    QuitCommand(const char *cmd_line);
    virtual ~QuitCommand() {}
    void execute() override;
};

class JobsList {
    /*
      Solution for the list is a map structure (std::map<int,JobEntry>) as the key is job id (int) and the data is the
      job entry object (JobEntry). The list implement:
        - each job id is unique
        - the list (map) sorted by job id.
        - job id cannot be change (const) but the job entry can be change.
    */

  public:
    class JobEntry {
      public:
        pid_t pid;
        jobState state;
        time_t entry_time; // Relative to epoch!!
        Command &cmd;
        JobEntry(pid_t pid, jobState state, Command &cmd);
        ~JobEntry();
        void printJob(int job_id);
    };

    typedef std::pair<const int, JobEntry> Job;
    typedef std::map<const int, JobEntry> JobsMap;

    // helpers for readability
    inline static const int &jobId(const Job &job) {
        return job.first;
    }
    inline static JobEntry &jobEntry(Job &job) {
        return job.second;
    }

  private:
    JobsMap jobs;

  public:
    Job *foreground = NULL;

  public:
    JobsList();
    ~JobsList();
    void addJob(Command *cmd, pid_t pid, bool isStopped = false);
    void removeJobById(int job_id);
    int removeFinishedJobs();
    Job *getJobById(int job_id);
    Job *getLastJob();
    Job *getLastStoppedJob();
    void killAllJobs();
    void printJobsList();
};

class JobsCommand : public BuiltInCommand {
  public:
    JobsCommand(const char *cmd_line);
    virtual ~JobsCommand() {}
    void execute() override;
};

class ForegroundCommand : public BuiltInCommand {
  public:
    ForegroundCommand(const char *cmd_line);
    virtual ~ForegroundCommand() {}
    void execute() override;
};

class BackgroundCommand : public BuiltInCommand {
  public:
    BackgroundCommand(const char *cmd_line);
    virtual ~BackgroundCommand() {}
    void execute() override;
};

class TimeoutCommand : public BuiltInCommand {
    /* Optional */
    // TODO: Add your data members
  public:
    explicit TimeoutCommand(const char *cmd_line);
    virtual ~TimeoutCommand() {}
    void execute() override;
};

class FareCommand : public BuiltInCommand {
    /* Optional */
    // TODO: Add your data members
  public:
    FareCommand(const char *cmd_line);
    virtual ~FareCommand() {}
    void execute() override;
};

class SetcoreCommand : public BuiltInCommand {
    /* Optional */
    // TODO: Add your data members
  public:
    SetcoreCommand(const char *cmd_line);
    virtual ~SetcoreCommand() {}
    void execute() override;
};

class KillCommand : public BuiltInCommand {
  public:
    KillCommand(const char *cmd_line);
    virtual ~KillCommand() {}
    void execute() override;
};

class SmallShell {
  public:
    bool quit = false;
    string display_prompt;
    string last_pwd; // always absolute path
    JobsList jobs_list;

  private:
    SmallShell();

  public:
    SmallShell(SmallShell const &) = delete; // disable copy ctor
    ~SmallShell();
    void operator=(SmallShell const &) = delete; // disable = operator
    static SmallShell &getInstance()             // make SmallShell singleton
    {
        static SmallShell instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }
    Command *CreateCommand(const char *cmd_line);
    void executeCommand(const char *cmd_line);
};

#endif // SMASH_COMMAND_H_
