#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define max_arguments 512
#define max_length = 2040

// shell status
int status = 0;

// number of pid's in background and the array holding them
int pidCount = 0;
int bgPID[200];

// flag for if program is in foreground-only mode
int fgLock = 0;

/* struct for command elements information */
struct command
{
    char *command;
    char *args[512];
    char *input;
    char *output;
    int *background;
};

/*
 * Reset the command struct
 */
void initCommand(struct command *c)

{
    for (int i = 0; i <= max_arguments; i++)
    {
        c->args[i] = NULL;
    }
    c->input = NULL;
    c->output = NULL;
    c->background = 0;
    c->command = NULL;
}

/*
 * Process the command by tokenizing the string and storing the results in the appropriate
 * variables in the command struct.
 */
int processCommand(char *input, struct command *com)
{

    int count = 0;
    char *saveptr;
    char *token;
    char *token2;
    int len = strlen(input);
    char last = input[len - 2];
    char buff[250];
    char buff2[250];

    // Check to see if the user entered a blank line or a comment
    if (input[0] == '\n' || input[0] == '#')
    {
        otherCommands(com);
        getUserInput();
    }

    // if the last value is & then set the bacground flag and remove
    if (input[len - 2] == '&')
    {
        // check to see if we are locked into foreground mode
        if (fgLock == 0)
        {
            com->background = 1;
        }
        input[len - 2] = '\0';
    }

    // The first token = the command
    token = strtok_r(input, " ", &saveptr);

    // loop through and extract the rest of the tokens
    while (token != NULL)
    {
        if (strcmp(token, "<") == 0)
        {

            token = strtok_r(NULL, " ", &saveptr);
            com->input = calloc(strlen(token) + 1, sizeof(char));
            strcpy(com->input, token);
        }
        if (strcmp(token, ">") == 0)
        {

            token = strtok_r(NULL, " ", &saveptr);
            com->output = calloc(strlen(token) + 1, sizeof(char));
            strcpy(com->output, token);
        }

        // check for expansion and handle
        if (strcmp(token, " <>") != 0)
        {
            if (strstr(token, "$$") != NULL)
            {

                pid_t pid = getpid();
                char *temp = token;
                sprintf(buff, "%d", pid);

                // get string up until $$
                token2 = strtok(temp, " $");

                // add pid to end of new token
                strcat(token2, buff);

                // Make savepointer the new string
                saveptr = token2;
                token = strtok_r(NULL, " ", &saveptr);

                // add token to array
                com->args[count] = token2;
                token = strtok_r(NULL, " ", &saveptr);
            }
            else
            {
                com->args[count] = strdup(token);
                token = strtok_r(NULL, " ", &saveptr);
            }

            if (com->args[count][strlen(com->args[count]) - 1] == '\n')
            {
                com->args[count][strlen(com->args[count]) - 1] = '\0';
            }

            count++;
        }
    }
    com->command = com->args[0];

    // strip newline character if it exists
    if (com->command[strlen(com->command) - 1] == '\n')
    {
        com->command[strlen(com->command) - 1] = '\0';
    }
    if (com->input != NULL)
    {
        (com->input[strcspn(com->input, "\n")] = 0);
    }
    if (com->output != NULL)
    {
        (com->output[strcspn(com->output, "\n")] = 0);
    }

    // If built in command is present then go process it
    if ((strcmp(com->command, "exit") == 0) || (strcmp(com->command, "cd") == 0) || (strcmp(com->command, "status") == 0))
    {
        builtInCommands(com);
    }
    else
    {
        // If a built in command is not found then go to execute other commands
        otherCommands(com);
    }
}

/*
 * Function for built-in commands of exit, cd, and status
 */
void builtInCommands(struct command *com)
{

    // Check for exit command
    if (strstr(com->command, "exit") != NULL)
    {
        fflush(stdout);
        for (int i = 0; i < pidCount; i++)
        {

            kill(bgPID[i], 15);
        }
        printf("Exiting Shell\n");
        fflush(stdout);
        exit(EXIT_SUCCESS);
    }

    /*
     * Check for cd command
     * the cd command can take one argument at most,
     * if no arguments then cd to home directory
     */
    if (strcmp(com->command, "cd") == 0)
    {
        char dir[100];

        if (com->args[1] == NULL)
        {
            chdir(getenv("HOME"));
            printf("%s\n", getcwd(dir, 100));
            fflush(stdout);
        }
        else
        {
            chdir(com->args[1]);
            printf("%s\n", getcwd(dir, 100));
            fflush(stdout);
        }
    }
    // Check for status command
    if (strcmp(com->command, "status") == 0)
    {
        printf("Exit value %d\n", status);
        fflush(stdout);
    }

    getUserInput();
}

/*
 * Process non built-in commands by by forking a new child process and call
 * execvp with the command and args array as input values
 */

void otherCommands(struct command *com)
{
    int childStatus;
    int result;
    int *pid[100];
    struct sigaction sa_sigint = {0};

    // Fork a new process
    pid_t spawnPid = fork();

    switch (spawnPid)
    {
    case -1:
        perror("fork()\n");
        status = 1;
        break;
    case 0:
        // In the child process
        if (com->background == 0)
        {

            // set control C to default action
            sa_sigint.sa_handler = SIG_DFL;
            sigaction(SIGINT, &sa_sigint, NULL);
        }
        if (com->background == 1)
        {
            if (com->input == NULL)
            {

                // if source file is not present then redirect stdin to /dev/null
                int sourceFD = open("/dev/null", O_RDONLY);
                if (sourceFD == -1)
                {
                    perror("source open()");
                    status = 1;
                    exit(1);
                }

                // Redirect stdin to source file
                result = dup2(sourceFD, 0);
                if (result == -1)
                {
                    perror("source dup2()");
                    status = 1;
                    exit(1);
                }
                close(sourceFD);
            }

            // if output file is not present then redirect stdout to /dev/null
            if (com->output == NULL)
            {

                // Open target file
                int targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0222);
                if (targetFD == -1)
                {
                    perror("target open()");
                    status = 1;
                    exit(1);
                }
                int result2 = dup2(targetFD, 1);

                if (result2 == -1)
                {
                    printf("Cannot open /dev/null for output\n");
                    fflush(stdout);
                    status = 1;
                    exit(1);
                }
                close(targetFD);
            }
        }

        // if input file is present then redirect stdin
        if (com->input != NULL)
        {

            // Open source file
            int sourceFD = open(com->input, O_RDONLY);
            if (sourceFD == -1)
            {
                perror("source open()");
                status = 1;
                exit(1);
            }

            // Redirect stdin to source file
            result = dup2(sourceFD, 0);
            if (result == -1)
            {
                printf("Cannot open file for input\n");
                fflush(stdout);
                status = 1;
                exit(1);
            }
            close(sourceFD);
        }

        // if output file is present then redirect stdout
        if (com->output != NULL)
        {

            // Open target file
            int targetFD = open(com->output, O_WRONLY | O_CREAT | O_TRUNC, 0222);
            if (targetFD == -1)
            {
                perror("target open()");
                status = 1;
                exit(1);
            }
            int result2 = dup2(targetFD, 1);

            if (result2 == -1)
            {
                printf("Cannot open file for input\n");
                fflush(stdout);
                status = 1;
                exit(1);
            }
            close(targetFD);
        }
        if (com->input != NULL || com->output != NULL)
        {

            execlp(com->command, com->command, com->input, com->output, (char *)NULL);
            perror("execvp");
            status = 1;
            exit(1);
        }
        if (com->input == NULL && com->output == NULL)
        {

            execvp(com->command, com->args);
            perror("execvp");
            status = 1;
            exit(1);
        }
        break;
    default:
        // In the parent process
        if (com->background == 1)
        {
            bgPID[pidCount] = spawnPid;
            pidCount++;
            printf("Backgrund PID is (%d)\n", spawnPid);
            fflush(stdout);
            spawnPid = waitpid(spawnPid, &childStatus, WNOHANG);
        }
        if (com->background == 0)
        {
            // Wait for child's termination
            spawnPid = waitpid(spawnPid, &childStatus, 0);
            if (childStatus > 0)
            {
                status = 1;
            }
            else
            {
                status = 0;
            }
            // notify of termination
            if (spawnPid > 0)
            {
                if (WIFSIGNALED(childStatus))
                {
                    if (WTERMSIG(childStatus) == 2)
                    {
                        printf("\nterminated by signal %d\n", WTERMSIG(childStatus));
                        fflush(stdout);
                    }
                }
            }
        }
        // Check to see if any child processes have finished
        while ((spawnPid = waitpid(-1, &childStatus, WNOHANG)) > 0)
        {
            if (WIFEXITED(childStatus))
            {
                // exited by status
                printf("Backgrund PID (%d) is done terminating. Exited with code %d\n",
                       spawnPid, WEXITSTATUS(childStatus));
                fflush(stdout);
            }
            else
            {
                // terminated by signal
                printf("Backgrund PID (%d) has been terminated with code %d\n",
                       spawnPid, WTERMSIG(childStatus));
                fflush(stdout);
            }

            // Remove pid from list
            for (int i = 0; i < strlen(bgPID); i++)
            {
                if (spawnPid == bgPID[i])
                {
                    bgPID[i] = bgPID[strlen(bgPID) - 1];
                    bgPID[strlen(bgPID) - 1] = '\0';
                    pidCount--;
                }
            }
        }
    }

    getUserInput();
}

/*
 * Function that displays the command promt and reads in the user input
 */
void getUserInput()
{

    char *input = NULL;
    size_t len = 0;

    printf(": ");
    fflush(stdout);

    // Read input
    getline(&input, &len, stdin);

    // reset the struct everytime we get new user input
    struct command *com = malloc(sizeof(struct command));
    initCommand(com);

    // Go handle the user input
    processCommand(input, com);
}

/*
 * Function handles the SIGTSTP handler for control Z
 * if the user selects control Z, the foreground-only
 * global flag is toggled on or off based on previous state
 */
void handle_sigtstp(int signo)
{
    // toggle on foreground-only mode
    if (fgLock == 0)
    {
        fgLock = 1;
        char *message = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write(STDOUT_FILENO, message, 52);
        fflush(stdout);
    }
    // toggle off
    else
    {
        fgLock = 0;
        char *message = "\nExiting foreground-only mode\n: ";
        write(STDOUT_FILENO, message, 32);
        fflush(stdout);
    }
}

/*
 * Main function that also specifies signal handlers for
 * control C and control Z functions
 */
int main(int argc, char *argv[])
{
    // Signal handler to ignore control C
    struct sigaction sa_sigint = {0};
    sa_sigint.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa_sigint, NULL);

    // Signal handler to redirect control Z
    struct sigaction sa_sigtstp = {0};
    sa_sigtstp.sa_handler = handle_sigtstp;
    sigfillset(&sa_sigtstp.sa_mask);
    sa_sigtstp.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa_sigtstp, NULL);

    getUserInput();

    return 0;
}