#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <inttypes.h>
#include <stdint.h>
#include <limits.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);

// Global variable to store the last command's exit status
int last_status = 0; 

// Forward declaration of the sigint_handler function
void sigint_handler(int sig);

void sigint_handler(int sig){
  return;
}


int main(int argc, char *argv[])
{
  FILE *input = stdin;
  char *input_fn = "(stdin)";
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  char *line = NULL;
  size_t n = 0;
  ssize_t line_len;

  /* Define signal action structures for original and new actions */
  struct sigaction SIGINT_oldact, SIGTSTP_oldact;
  struct sigaction SIGINT_action = {0}, ignore_action = {0};

  /* Set up the handler and signal mask for ignoring SIGTSTP */
  ignore_action.sa_handler = SIG_IGN; // Handler to ignore SIGTSTP
  sigfillset(&ignore_action.sa_mask); // Fill mask to block signals during this action
  ignore_action.sa_flags = 0; // No special flags are set

  /* Configure the SIGINT handler and its signal mask */
  SIGINT_action.sa_handler = sigint_handler; // Handler for SIGINT
  sigfillset(&SIGINT_action.sa_mask); // Fill mask to block signals during this action
  SIGINT_action.sa_flags = 0; // No special flags are set



  for (;;) {
prompt:;
    /* Manage background processes */
    int backgroundFlag = 0;
    check_background_processes();

    /* TODO: prompt */
    if (input == stdin) {

      char *prompt = getenv("PS1"); 
      if (prompt == NULL) prompt = "";
      fprintf(stderr, "%s", prompt);
      sigaction(SIGTSTP, &ignore_action, &SIGTSTP_oldact);
      sigaction(SIGINT, &SIGINT_action, &SIGINT_oldact);
    }

    /*Reading a line of input*/
    ssize_t line_len = getline(&line, &n, input);

    // Check for end of file
    if (feof(input)) {
        exit(EXIT_SUCCESS); // Exit successfully if end of file is reached
    }

    // Handling line read error
    if (line_len < 0) {
        if (errno == EINTR) {
            // Clear error and print a newline character if interrupted by a signal 
            clearerr(input);
            fprintf(stderr, "\n");
            goto prompt; // Jump back to the prompt label for retrying the input
        } else {
            // Handle other errors and exit with failure status
            perror(input_fn);
            exit(EXIT_FAILURE);
        }
    }

    if (line_len == 1 && strcmp(line, "\n") == 0) continue; // Empty line


    
    size_t nwords = wordsplit(line);

    for (size_t i = 0; i < nwords; ++i) {
      // fprintf(stderr, "Word %zu: %s\n", i, words[i]);
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      // fprintf(stderr, "Expanded Word %zu: %s\n", i, words[i]);
    }

    if (nwords == 0) continue;

    if (input == stdin){
      sigaction(SIGINT, &ignore_action, NULL); // Ignore SIGINT
    }

    /* Process background command */
    if (strcmp(words[nwords - 1], "&") == 0){
        backgroundFlag = 1;
        nwords--; 
    }

    /* Handle 'cd' command */
    if (strcmp(words[0], "cd") == 0) {
        int dirChangeStatus = -1;
        if (nwords > 1) dirChangeStatus = chdir(words[1]);
        else dirChangeStatus = chdir(getenv("HOME"));

        if (dirChangeStatus == -1) fprintf(stderr, "directory not found\n");
        continue;
    }

    /* Manage 'exit' command */
    if (strcmp(words[0], "exit") == 0) {
        if (nwords > 2) {
            fprintf(stderr, "too many arguments\n");
            goto prompt;
        }
        long exitValue = last_status;
        if (nwords == 2) {
            char *endPtr;
            exitValue = strtol(words[1], &endPtr, 10);
            if ((endPtr == words[1]) || (*endPtr != '\0')) {
                fprintf(stderr, "that is not a number\n");
                goto prompt;
            }
        }
        exit(exitValue);
    }

    // Forking a new process to execute the command
    int childStatus;
    pid_t childProcessId; // Child process ID
    childProcessId = fork();

    switch (childProcessId) {
        case -1:
            perror("fork() was unsuccessful!");
            continue;  // Assuming this is within a loop in the main function

        case 0: { // child process
            // Change the sigactions if the input is stdin
            if (input == stdin) {
                sigaction(SIGINT, &SIGINT_oldact, NULL);
                sigaction(SIGTSTP, &SIGTSTP_oldact, NULL);
            }

            // Parsing the words list for redirections and arguments
            char *args[nwords + 1];
            size_t args_len = 0;
            int childInput, childOutput;

            for (size_t i = 0; i < nwords; ++i) {
                if (strcmp(words[i], "<") == 0) {
                    childInput = open(words[i + 1], O_RDONLY | O_CLOEXEC);
                    if (childInput == -1) {
                        perror("failed to open file");
                        exit(1);
                    }
                    if (dup2(childInput, STDIN_FILENO) == -1) {
                        perror("failed to assign new input file");
                        exit(2);
                    }
                    i++;  // Skip the filename
                    continue;
                } else if (strcmp(words[i], ">") == 0) {
                    childOutput = open(words[i + 1], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
                    if (childOutput == -1) {
                        perror("failed to open output file");
                        exit(1);
                    }
                    if (dup2(childOutput, STDOUT_FILENO) == -1) {
                        perror("failed to assign new output file");
                        exit(2);
                    }
                    i++;  // Skip the filename
                    continue;
                } else if (strcmp(words[i], ">>") == 0) {
                    childOutput = open(words[i + 1], O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
                    if (childOutput == -1) {
                        perror("failed to open output file");
                        exit(1);
                    }
                    if (dup2(childOutput, STDOUT_FILENO) == -1) {
                        perror("failed to assign new output file");
                        exit(2);
                    }
                    i++;  // Skip the filename
                    continue;
                } else {
                    args[args_len++] = words[i];
                }
            }
            args[args_len] = NULL;

            // Execute the command
            execvp(args[0], args);
            perror("smallsh");
            _exit(EXIT_FAILURE);
        }
        default: {
          if (!backgroundFlag) {
            waitpid(childProcessId, &childStatus, WUNTRACED);

            int corrStatus;
            if (WIFSIGNALED(childStatus)){
              corrStatus = WTERMSIG(childStatus) + 128;
            }
            else if (WIFSTOPPED(childStatus)){
              char *bgPid;
              asprintf(&bgPid, "%jd", (intmax_t)childProcessId);
              setenv("BG_PID", bgPid, 1);
              fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)childProcessId);
              kill(childProcessId, 18); // Signal 18 = SIGCONT
              goto prompt;
            }
            else if (WIFEXITED(childStatus))
                corrStatus = WEXITSTATUS(childStatus);
            else
                corrStatus = WTERMSIG(childStatus);
            char *childStatusStr;
            asprintf(&childStatusStr, "%d", corrStatus);
            setenv("LATEST_FG", childStatusStr, 1);
        }
        else{
          waitpid(childProcessId, &childStatus, WNOHANG | WUNTRACED);
          char *bgPid;
          asprintf(&bgPid, "%jd", (intmax_t)childProcessId);
          setenv("BG_PID", bgPid, 1);
        }
    }
    }


  }
}

char *words[MAX_WORDS] = {0};


/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char **start, char **end)
{
  static char *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = NULL;
  *end = NULL;
  char *s = strchr(word, '$');
  if (s) {
    char *c = strchr("$!?", s[1]);
    if (c) {
      ret = *c;
      *start = s;
      *end = s + 2;
    }
    else if (s[1] == '{') {
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = '{';
        *start = s;
        *end = e + 1;
      }
    }
  }
  prev = *end;
  return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */

char *
expand(char const *word)
{
  char const *pos = word;
  char const *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    switch (c) {
      case '!': {
        char* bg_pid = getenv("BG_PID");
        if (!bg_pid) build_str("", NULL);
        else build_str(bg_pid, NULL);
        break;
      }
      case '$': {
        char* pid;
        asprintf(&pid, "%jd", (intmax_t)getpid());
        if (pid < 0) err(1, "getpid() or asprintf() failure");
        build_str(pid, NULL);
        break;
      }
      case '?': {
        char* fg_status = getenv("LATEST_FG");
        if (!fg_status) build_str("0", NULL);
        else build_str(fg_status, NULL);
        break;
      }
      case '{': {
        size_t length = end - 1 - (start + 2);
        char temp[length + 1];
        memcpy(temp, start + 2, length);
        temp[length] = '\0';

        char* value = getenv(temp);
        if (!value) build_str("", NULL);
        else build_str(value, NULL);
        break;
      }
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}


/*Function checking background processes*/
void check_background_processes() {
    pid_t backgroundChildProcessId;   // Background child process ID
    int backgroundChildStatus;  // Status of the background child process
    int correctedBackgroundStatus;   // Corrected status of the background child process

    while ((backgroundChildProcessId = waitpid(0, &backgroundChildStatus, WNOHANG | WUNTRACED)) > 0) {
        if (WIFEXITED(backgroundChildStatus)) {
            correctedBackgroundStatus = WEXITSTATUS(backgroundChildStatus);
            fprintf(stderr, "Child process %jd done. Exit status %d.\n",
                    (intmax_t)backgroundChildProcessId, correctedBackgroundStatus);
        } else if (WIFSIGNALED(backgroundChildStatus)) {
            correctedBackgroundStatus = WTERMSIG(backgroundChildStatus);
            fprintf(stderr, "Child process %jd done. Signaled %d.\n",
                    (intmax_t)backgroundChildProcessId, correctedBackgroundStatus);
        } else if (WIFSTOPPED(backgroundChildStatus)) {
            // Send SIGCONT signal to that process
            fprintf(stderr, "Child process %jd stopped. Continuing.\n",
                    (intmax_t)backgroundChildProcessId);
            kill(backgroundChildProcessId, SIGCONT);
        }
    }
}


