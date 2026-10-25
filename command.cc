/*
 * command.cc - Complete implementation for Lab 3 Mini Shell
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <errno.h>
#include <limits.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"

static char logFilePath[PATH_MAX] = "log.txt";

static void writeChildLog(pid_t pid)
{
    FILE *logFile = fopen(logFilePath, "a");
    if (logFile)
    {
        time_t now = time(NULL);
        char *timeStr = ctime(&now);
        if (timeStr)
        {
            size_t len = strlen(timeStr);
            if (len > 0 && timeStr[len - 1] == '\n')
            {
                timeStr[len - 1] = '\0';
            }
        }
        if (pid > 0)
        {
            fprintf(logFile, "[%s] Child process %d terminated\n",
                    timeStr ? timeStr : "unknown time", (int)pid);
        }
        else
        {
            fprintf(logFile, "[%s] Child process terminated\n",
                    timeStr ? timeStr : "unknown time");
        }
        fclose(logFile);
    }
}

static void sigchldHandler(int sig)
{
    (void)sig;
    int savedErrno = errno;
    pid_t pid;
    int reaped = 0;

    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
    {
        reaped++;
        writeChildLog(pid);
    }

    if (reaped == 0)
    {
        writeChildLog(0);
    }

    errno = savedErrno;
}

static void sigintHandler(int sig)
{
    (void)sig;
    printf("\nmyshell>");
    fflush(stdout);
}

static bool isFilenameToken(const Token &t)
{
    return t.type == TOKEN_ARGUMENT || t.type == TOKEN_COMMAND;
}

static void insertExpandedArgument(SimpleCommand *cmd, const std::string &arg, bool wasQuoted)
{
    if (!wasQuoted && (arg.find('*') != std::string::npos || arg.find('?') != std::string::npos))
    {
        glob_t globbuf;
        int ret = glob(arg.c_str(), 0, NULL, &globbuf);
        if (ret == 0)
        {
            for (size_t i = 0; i < globbuf.gl_pathc; i++)
            {
                cmd->insertArgument(strdup(globbuf.gl_pathv[i]));
            }
            globfree(&globbuf);
            return;
        }
        globfree(&globbuf);
    }
    cmd->insertArgument(strdup(arg.c_str()));
}

void parse(std::vector<Token> &tokens)
{
    if (tokens.empty() || tokens[0].type == TOKEN_EOF)
    {
        return;
    }

    Command &currentCommand = Command::_currentCommand;
    currentCommand.clear();
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token &t = tokens[i];

        if (t.type == TOKEN_EOF)
        {
            break;
        }
        else if (t.type == TOKEN_COMMAND || t.type == TOKEN_ARGUMENT)
        {
            std::string val = t.value;
            bool wasQuoted = false;

            if (!val.empty() && (val.front() == '"' || val.front() == '\''))
            {
                char quoteChar = val.front();
                wasQuoted = true;

                while ((val.size() < 2 || val.back() != quoteChar) &&
                       (i + 1 < tokens.size()) &&
                       (tokens[i + 1].type == TOKEN_ARGUMENT || tokens[i + 1].type == TOKEN_COMMAND))
                {
                    i++;
                    val += " " + tokens[i].value;
                }

                if (val.size() >= 2 && val.front() == quoteChar && val.back() == quoteChar)
                {
                    val = val.substr(1, val.size() - 2);
                }
            }

            insertExpandedArgument(currentSimpleCommand, val, wasQuoted);
        }
        else if (t.type == TOKEN_PIPE)
        {
            if (currentSimpleCommand->_numberOfArguments == 0)
            {
                fprintf(stderr, "Syntax error: missing command before '|'\n");
                free(currentSimpleCommand->_arguments);
                free(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            currentCommand.insertSimpleCommand(currentSimpleCommand);
            currentSimpleCommand = new SimpleCommand();

            if (i + 1 >= tokens.size() || tokens[i + 1].type == TOKEN_EOF ||
                tokens[i + 1].type == TOKEN_PIPE || tokens[i + 1].type == TOKEN_BACKGROUND)
            {
                fprintf(stderr, "Syntax error: missing command after '|'\n");
                free(currentSimpleCommand->_arguments);
                free(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
        }
        else if (t.type == TOKEN_REDIRECT)
        {
            if (i + 1 >= tokens.size() || !isFilenameToken(tokens[i + 1]))
            {
                fprintf(stderr, "Syntax error: missing filename after '>'\n");
                free(currentSimpleCommand->_arguments);
                free(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._outFile)
            {
                free(currentCommand._outFile);
            }
            currentCommand._outFile = strdup(tokens[i].value.c_str());
            currentCommand._append = 0;
        }
        else if (t.type == TOKEN_APPEND)
        {
            if (i + 1 >= tokens.size() || !isFilenameToken(tokens[i + 1]))
            {
                fprintf(stderr, "Syntax error: missing filename after '>>'\n");
                free(currentSimpleCommand->_arguments);
                free(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._outFile)
            {
                free(currentCommand._outFile);
            }
            currentCommand._outFile = strdup(tokens[i].value.c_str());
            currentCommand._append = 1;
        }
        else if (t.type == TOKEN_INPUT)
        {
            if (i + 1 >= tokens.size() || !isFilenameToken(tokens[i + 1]))
            {
                fprintf(stderr, "Syntax error: missing filename after '<'\n");
                free(currentSimpleCommand->_arguments);
                free(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._inputFile)
            {
                free(currentCommand._inputFile);
            }
            currentCommand._inputFile = strdup(tokens[i].value.c_str());
        }
        else if (t.type == TOKEN_ERROR)
        {
            if (i + 1 >= tokens.size() || !isFilenameToken(tokens[i + 1]))
            {
                fprintf(stderr, "Syntax error: missing filename after '2>'\n");
                free(currentSimpleCommand->_arguments);
                free(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._errFile)
            {
                free(currentCommand._errFile);
            }
            currentCommand._errFile = strdup(tokens[i].value.c_str());
        }
        else if (t.type == TOKEN_REDIRECT_AND_ERROR)
        {
            if (i + 1 >= tokens.size() || !isFilenameToken(tokens[i + 1]))
            {
                fprintf(stderr, "Syntax error: missing filename after '>>&'\n");
                free(currentSimpleCommand->_arguments);
                free(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._outFile)
            {
                free(currentCommand._outFile);
            }
            if (currentCommand._errFile)
            {
                free(currentCommand._errFile);
            }
            currentCommand._outFile = strdup(tokens[i].value.c_str());
            currentCommand._errFile = strdup(tokens[i].value.c_str());
            currentCommand._out_error = 1;
            currentCommand._append = 1;
        }
        else if (t.type == TOKEN_BACKGROUND)
        {
            if (currentSimpleCommand->_numberOfArguments == 0 &&
                currentCommand._numberOfSimpleCommands == 0)
            {
                fprintf(stderr, "Syntax error: unexpected '&'\n");
                free(currentSimpleCommand->_arguments);
                free(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            currentCommand._background = 1;
        }
    }

    if (currentSimpleCommand->_numberOfArguments > 0)
    {
        currentCommand.insertSimpleCommand(currentSimpleCommand);
    }
    else
    {
        free(currentSimpleCommand->_arguments);
        free(currentSimpleCommand);
    }

    if (currentCommand._numberOfSimpleCommands == 0)
    {
        fprintf(stderr, "Syntax error: missing command\n");
        currentCommand.clear();
        return;
    }

    currentCommand.execute();
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _out_error = 0;
    _background = 0;
    _append = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        free(_simpleCommands[i]);
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile && _errFile != _outFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _out_error = 0;
    _background = 0;
    _append = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
        printf("\n");
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default",
           _background ? "YES" : "NO");
    printf("\n\n");
}

void Command::execute()
{
    if (_numberOfSimpleCommands == 0)
    {
        clear();
        return;
    }

    // Built-in command: exit
    if (strcmp(_simpleCommands[0]->_arguments[0], "exit") == 0)
    {
        printf("Good bye!!\n");
        clear();
        exit(0);
    }

    print();

    // Built-in command: cd [dir]
    if (strcmp(_simpleCommands[0]->_arguments[0], "cd") == 0)
    {
        const char *dir = NULL;
        if (_simpleCommands[0]->_numberOfArguments > 1)
        {
            dir = _simpleCommands[0]->_arguments[1];
        }
        else
        {
            dir = getenv("HOME");
        }

        if (!dir || chdir(dir) != 0)
        {
            perror("cd");
        }
        clear();
        return;
    }

    // Save default stdin, stdout, stderr
    int defaultin = dup(0);
    int defaultout = dup(1);
    int defaulterr = dup(2);

    // Setup error redirection if specified (and not shared via >>&)
    int fderr;
    if (_errFile && !_out_error)
    {
        int errFlags = O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC);
        fderr = open(_errFile, errFlags, 0666);
        if (fderr < 0)
        {
            perror("myshell: error file");
            dup2(defaultin, 0);
            dup2(defaultout, 1);
            dup2(defaulterr, 2);
            close(defaultin);
            close(defaultout);
            close(defaulterr);
            clear();
            return;
        }
    }
    else
    {
        fderr = dup(defaulterr);
    }
    dup2(fderr, 2);
    close(fderr);

    // Setup initial input
    int fdin;
    if (_inputFile)
    {
        fdin = open(_inputFile, O_RDONLY);
        if (fdin < 0)
        {
            perror("myshell: input file");
            dup2(defaultin, 0);
            dup2(defaultout, 1);
            dup2(defaulterr, 2);
            close(defaultin);
            close(defaultout);
            close(defaulterr);
            clear();
            return;
        }
    }
    else
    {
        fdin = dup(defaultin);
    }

    std::vector<pid_t> childPids;
    int fdout;

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        // Redirect input for current simple command
        dup2(fdin, 0);
        close(fdin);

        // Setup output for current simple command
        if (i == _numberOfSimpleCommands - 1)
        {
            if (_outFile)
            {
                int outFlags = O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC);
                fdout = open(_outFile, outFlags, 0666);
                if (fdout < 0)
                {
                    perror("myshell: output file");
                    dup2(defaultin, 0);
                    dup2(defaultout, 1);
                    dup2(defaulterr, 2);
                    close(defaultin);
                    close(defaultout);
                    close(defaulterr);
                    clear();
                    return;
                }
            }
            else
            {
                fdout = dup(defaultout);
            }
        }
        else
        {
            int fdpipe[2];
            if (pipe(fdpipe) == -1)
            {
                perror("myshell: pipe");
                dup2(defaultin, 0);
                dup2(defaultout, 1);
                dup2(defaulterr, 2);
                close(defaultin);
                close(defaultout);
                close(defaulterr);
                clear();
                return;
            }
            fdout = fdpipe[1];
            fdin = fdpipe[0];
        }

        // Redirect output
        dup2(fdout, 1);
        if (i == _numberOfSimpleCommands - 1 && _out_error)
        {
            dup2(fdout, 2);
        }
        close(fdout);

        // Create child process
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("myshell: fork");
            if (i < _numberOfSimpleCommands - 1)
            {
                close(fdin);
            }
            dup2(defaultin, 0);
            dup2(defaultout, 1);
            dup2(defaulterr, 2);
            close(defaultin);
            close(defaultout);
            close(defaulterr);
            clear();
            return;
        }

        if (pid == 0)
        {
            // Child process
            signal(SIGINT, SIG_DFL);

            if (i < _numberOfSimpleCommands - 1)
            {
                close(fdin);
            }
            close(defaultin);
            close(defaultout);
            close(defaulterr);

            execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);
            perror(_simpleCommands[i]->_arguments[0]);
            _exit(1);
        }

        childPids.push_back(pid);
    }

    // Restore default stdin, stdout, stderr in parent
    dup2(defaultin, 0);
    dup2(defaultout, 1);
    dup2(defaulterr, 2);
    close(defaultin);
    close(defaultout);
    close(defaulterr);

    // Wait for foreground processes to finish
    if (!_background)
    {
        for (size_t i = 0; i < childPids.size(); i++)
        {
            int status;
            while (waitpid(childPids[i], &status, 0) == -1)
            {
                if (errno != EINTR)
                {
                    break;
                }
            }
        }
    }

    clear();
}

void Command::prompt()
{
    std::string input;
    while (true)
    {
        printf("myshell>");
        fflush(stdout);

        if (!std::getline(std::cin, input))
        {
            if (std::cin.eof())
            {
                printf("\n");
                break;
            }
            std::cin.clear();
            continue;
        }

        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        snprintf(logFilePath, sizeof(logFilePath), "%s/log.txt", cwd);
    }

    struct sigaction saInt;
    memset(&saInt, 0, sizeof(saInt));
    saInt.sa_handler = sigintHandler;
    sigemptyset(&saInt.sa_mask);
    saInt.sa_flags = SA_RESTART;
    sigaction(SIGINT, &saInt, NULL);

    struct sigaction saChld;
    memset(&saChld, 0, sizeof(saChld));
    saChld.sa_handler = sigchldHandler;
    sigemptyset(&saChld.sa_mask);
    saChld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &saChld, NULL);

    Command::_currentCommand.prompt();
    return 0;
}
