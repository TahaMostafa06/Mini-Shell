/*
 * Template file.
 * Complete implementation of command parsing, execution, pipes,
 * redirection, built-in commands, signal handling, and wildcarding.
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
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"

// Signal handler for SIGCHLD to log child process termination
void handle_sigchld(int sig)
{
    (void)sig;
    int saved_errno = errno;

    // Reap any terminated background child processes
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }

    int fd = open("log.txt", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0)
    {
        time_t now = time(NULL);
        char *time_str = ctime(&now);
        char buffer[256];
        if (time_str)
        {
            size_t len = strlen(time_str);
            if (len > 0 && time_str[len - 1] == '\n')
                time_str[len - 1] = '\0';
            int n = snprintf(buffer, sizeof(buffer), "[%s] Child process terminated\n", time_str);
            if (n > 0)
                write(fd, buffer, n);
        }
        else
        {
            const char *msg = "Child process terminated\n";
            write(fd, msg, strlen(msg));
        }
        close(fd);
    }

    errno = saved_errno;
}

// Helper to expand wildcards (* and ?) for Bonus Part
static void insertArgumentWithWildcards(SimpleCommand *cmd, const std::string &arg, bool wasQuoted)
{
    if (!wasQuoted && (arg.find('*') != std::string::npos || arg.find('?') != std::string::npos))
    {
        glob_t glob_result;
        memset(&glob_result, 0, sizeof(glob_result));
        int ret = glob(arg.c_str(), 0, NULL, &glob_result);
        if (ret == 0 && glob_result.gl_pathc > 0)
        {
            for (size_t i = 0; i < glob_result.gl_pathc; i++)
            {
                cmd->insertArgument(strdup(glob_result.gl_pathv[i]));
            }
            globfree(&glob_result);
            return;
        }
        globfree(&glob_result);
    }
    cmd->insertArgument(strdup(arg.c_str()));
}

void parse(std::vector<Token> &tokens)
{
    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token &token = tokens[i];

        if (token.type == TOKEN_EOF)
        {
            break;
        }
        else if (token.type == TOKEN_COMMAND || token.type == TOKEN_ARGUMENT)
        {
            std::string arg = token.value;
            bool wasQuoted = false;

            // Reconstruct quoted strings split by whitespace and strip surrounding quotes
            if (!arg.empty() && (arg.front() == '"' || arg.front() == '\''))
            {
                char quote = arg.front();
                wasQuoted = true;
                while ((arg.size() < 2 || arg.back() != quote) &&
                       i + 1 < tokens.size() && tokens[i + 1].type != TOKEN_EOF)
                {
                    i++;
                    arg += " " + tokens[i].value;
                }
                if (arg.size() >= 2 && arg.front() == quote && arg.back() == quote)
                {
                    arg = arg.substr(1, arg.size() - 2);
                }
                else if (arg.front() == quote)
                {
                    arg = arg.substr(1);
                }
            }

            insertArgumentWithWildcards(currentSimpleCommand, arg, wasQuoted);
        }
        else if (token.type == TOKEN_PIPE)
        {
            if (currentSimpleCommand->_numberOfArguments == 0 ||
                i + 1 >= tokens.size() ||
                tokens[i + 1].type == TOKEN_EOF ||
                tokens[i + 1].type == TOKEN_PIPE ||
                tokens[i + 1].type == TOKEN_BACKGROUND)
            {
                fprintf(stderr, "Syntax error: invalid pipe usage\n");
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            currentCommand.insertSimpleCommand(currentSimpleCommand);
            currentSimpleCommand = new SimpleCommand();
        }
        else if (token.type == TOKEN_REDIRECT)
        {
            if (i + 1 >= tokens.size() ||
                (tokens[i + 1].type != TOKEN_ARGUMENT && tokens[i + 1].type != TOKEN_COMMAND))
            {
                fprintf(stderr, "Syntax error: missing filename after '>'\n");
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._outFile)
                free(currentCommand._outFile);
            currentCommand._outFile = strdup(tokens[i].value.c_str());
            currentCommand._append = 0;
        }
        else if (token.type == TOKEN_APPEND)
        {
            if (i + 1 >= tokens.size() ||
                (tokens[i + 1].type != TOKEN_ARGUMENT && tokens[i + 1].type != TOKEN_COMMAND))
            {
                fprintf(stderr, "Syntax error: missing filename after '>>'\n");
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._outFile)
                free(currentCommand._outFile);
            currentCommand._outFile = strdup(tokens[i].value.c_str());
            currentCommand._append = 1;
        }
        else if (token.type == TOKEN_INPUT)
        {
            if (i + 1 >= tokens.size() ||
                (tokens[i + 1].type != TOKEN_ARGUMENT && tokens[i + 1].type != TOKEN_COMMAND))
            {
                fprintf(stderr, "Syntax error: missing filename after '<'\n");
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._inputFile)
                free(currentCommand._inputFile);
            currentCommand._inputFile = strdup(tokens[i].value.c_str());
        }
        else if (token.type == TOKEN_ERROR)
        {
            if (i + 1 >= tokens.size() ||
                (tokens[i + 1].type != TOKEN_ARGUMENT && tokens[i + 1].type != TOKEN_COMMAND))
            {
                fprintf(stderr, "Syntax error: missing filename after '2>'\n");
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._errFile)
                free(currentCommand._errFile);
            currentCommand._errFile = strdup(tokens[i].value.c_str());
        }
        else if (token.type == TOKEN_REDIRECT_AND_ERROR)
        {
            if (i + 1 >= tokens.size() ||
                (tokens[i + 1].type != TOKEN_ARGUMENT && tokens[i + 1].type != TOKEN_COMMAND))
            {
                fprintf(stderr, "Syntax error: missing filename after '>>&'\n");
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentCommand.clear();
                return;
            }
            i++;
            if (currentCommand._outFile)
                free(currentCommand._outFile);
            if (currentCommand._errFile)
                free(currentCommand._errFile);
            currentCommand._outFile = strdup(tokens[i].value.c_str());
            currentCommand._errFile = strdup(tokens[i].value.c_str());
            currentCommand._out_error = 1;
            currentCommand._append = 1;
        }
        else if (token.type == TOKEN_BACKGROUND)
        {
            if ((currentSimpleCommand->_numberOfArguments == 0 && currentCommand._numberOfSimpleCommands == 0) ||
                (i + 1 < tokens.size() && tokens[i + 1].type != TOKEN_EOF))
            {
                fprintf(stderr, "Syntax error: invalid background operator '&'\n");
                currentCommand.insertSimpleCommand(currentSimpleCommand);
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
        if (currentCommand._outFile || currentCommand._inputFile || currentCommand._errFile)
        {
            fprintf(stderr, "Syntax error: redirection without command\n");
        }
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
    _numberOfSimpleCommands = 0;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

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

        if (dir == NULL || chdir(dir) != 0)
        {
            perror("cd");
        }
        clear();
        return;
    }

    // Save default stdin, stdout, stderr
    int tmpin = dup(0);
    int tmpout = dup(1);
    int tmperr = dup(2);

    // Setup error redirection
    int fderr;
    if (_errFile)
    {
        int flags = O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC);
        fderr = open(_errFile, flags, 0666);
        if (fderr < 0)
        {
            perror(_errFile);
            close(tmpin);
            close(tmpout);
            close(tmperr);
            clear();
            return;
        }
    }
    else
    {
        fderr = dup(tmperr);
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
            perror(_inputFile);
            dup2(tmpin, 0);
            dup2(tmpout, 1);
            dup2(tmperr, 2);
            close(tmpin);
            close(tmpout);
            close(tmperr);
            clear();
            return;
        }
    }
    else
    {
        fdin = dup(tmpin);
    }

    pid_t ret = -1;
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
                if (_out_error == 1)
                {
                    fdout = dup(2);
                }
                else
                {
                    int flags = O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC);
                    fdout = open(_outFile, flags, 0666);
                }

                if (fdout < 0)
                {
                    perror(_outFile);
                    dup2(tmpin, 0);
                    dup2(tmpout, 1);
                    dup2(tmperr, 2);
                    close(tmpin);
                    close(tmpout);
                    close(tmperr);
                    clear();
                    return;
                }
            }
            else
            {
                fdout = dup(tmpout);
            }
        }
        else
        {
            // Create pipe to next command
            int fdpipe[2];
            if (pipe(fdpipe) < 0)
            {
                perror("pipe");
                dup2(tmpin, 0);
                dup2(tmpout, 1);
                dup2(tmperr, 2);
                close(tmpin);
                close(tmpout);
                close(tmperr);
                clear();
                return;
            }
            fdout = fdpipe[1];
            fdin = fdpipe[0];
        }

        // Redirect output
        dup2(fdout, 1);
        close(fdout);

        // Fork child process
        ret = fork();
        if (ret < 0)
        {
            perror("fork");
            if (i < _numberOfSimpleCommands - 1)
            {
                close(fdin);
            }
            break;
        }
        else if (ret == 0)
        {
            // Child process
            close(tmpin);
            close(tmpout);
            close(tmperr);
            if (i < _numberOfSimpleCommands - 1)
            {
                close(fdin);
            }

            if (!_background)
            {
                signal(SIGINT, SIG_DFL);
            }

            execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);
            perror(_simpleCommands[i]->_arguments[0]);
            _exit(1);
        }
    }

    // Restore default stdin, stdout, stderr in the shell
    dup2(tmpin, 0);
    dup2(tmpout, 1);
    dup2(tmperr, 2);
    close(tmpin);
    close(tmpout);
    close(tmperr);

    // Wait for the last simple command if not running in background
    if (!_background && ret > 0)
    {
        waitpid(ret, NULL, 0);
    }

    clear();
}

void Command::prompt()
{
    printf("myshell>");
    fflush(stdout);
    std::string input;
    while (std::getline(std::cin, input))
    {
        if (!input.empty())
        {
            std::vector<Token> tokens = tokenize(input);
            parse(tokens);
        }
        printf("myshell>");
        fflush(stdout);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    // Ignore Ctrl-C (SIGINT) in the shell
    signal(SIGINT, SIG_IGN);

    // Log child terminations via SIGCHLD
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    Command::_currentCommand.prompt();
    return 0;
}