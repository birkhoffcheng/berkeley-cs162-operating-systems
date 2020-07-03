#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
	cmd_fun_t *fun;
	char *cmd;
	char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
	{cmd_help, "?", "show this help menu"},
	{cmd_exit, "exit", "exit the command shell"},
	{cmd_pwd, "pwd", "print working directory"},
	{cmd_cd, "cd", "change working directory"}
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
	for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
		printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
	return 1;
}

/* Exits this shell */
int cmd_exit(struct tokens *tokens) {
	tokens_destroy(tokens);
	exit(0);
}

int cmd_pwd(struct tokens *tokens) {
	char *cwd = getcwd(NULL, 0);
	printf("%s\n", cwd);
	free(cwd);
	return 0;
}

int cmd_cd(struct tokens *tokens) {
	size_t argc = tokens_get_length(tokens);
	if (argc > 2) {
		fprintf(stderr, "cd: too many arguments\n");
	}
	else if (argc == 2) {
		if (!chdir(tokens_get_token(tokens, 1)))
			return 0;
		perror("cd");
	}
	else {
		if (!chdir(getenv("HOME")))
			return 0;
		perror("cd");
	}
	return 0;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
	for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
		if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
			return i;
	return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
	/* Our shell is connected to standard input. */
	shell_terminal = STDIN_FILENO;

	/* Check if we are running interactively */
	shell_is_interactive = isatty(shell_terminal);

	if (shell_is_interactive) {
		/* If the shell is not currently in the foreground, we must pause the shell until it becomes a
		 * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
		 * foreground, we'll receive a SIGCONT. */
		while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
			kill(-shell_pgid, SIGTTIN);

		/* Saves the shell's process id */
		shell_pgid = getpid();

		/* Take control of the terminal */
		tcsetpgrp(shell_terminal, shell_pgid);

		/* Save the current termios to a variable, so it can be restored later. */
		tcgetattr(shell_terminal, &shell_tmodes);
	}
}

char *search_path(char *command) {
	struct stat stat_buf;
	size_t len = strlen(command);
	char *pathname;
	char *path_orig = getenv("PATH");
	if (path_orig == NULL)
		return NULL;
	char *path = strcpy(malloc(strlen(path_orig) + 1), path_orig);
	for (char *token = strtok(path, ":"); token != NULL; token = strtok(NULL, ":")) {
		pathname = strcpy(malloc(strlen(token) + len + 2), token);
		strcat(pathname, "/");
		strcat(pathname, command);
		if (stat(pathname, &stat_buf) != -1) {
			free(path);
			return pathname;
		}
		free(pathname);
	}
	free(path);
	return NULL;
}

int main(unused int argc, unused char *argv[]) {
	init_shell();

	static char line[4096];
	int line_num = 0, wstatus;
	struct stat stat_buf;
	pid_t pid;
	char *pathname;

	/* Please only print shell prompts when standard input is not a tty */
	if (shell_is_interactive)
		fprintf(stdout, "%d: ", line_num);

	while (fgets(line, 4096, stdin)) {
		/* Split our line into words. */
		struct tokens *tokens = tokenize(line);

		/* Find which built-in function to run. */
		int fundex = lookup(tokens_get_token(tokens, 0));

		if (fundex >= 0) {
			cmd_table[fundex].fun(tokens);
		}
		else if (tokens_get_length(tokens) > 0 && stat(tokens_get_token(tokens, 0), &stat_buf) != -1) {
			if ((pid = fork())) {
				waitpid(pid, &wstatus, 0);
			}
			else {
				char **args = malloc(sizeof(char *) * (tokens_get_length(tokens) + 1));
				for (size_t i = 0; i < tokens_get_length(tokens); i++) {
					args[i] = tokens_get_token(tokens, i);
				}
				args[tokens_get_length(tokens)] = NULL;
				execv(tokens_get_token(tokens, 0), args);
				perror(tokens_get_token(tokens, 0));
				return 1;
			}
		}
		else if (tokens_get_length(tokens) > 0 && (pathname = search_path(tokens_get_token(tokens, 0)))) {
			if ((pid = fork())) {
				free(pathname);
				waitpid(pid, &wstatus, 0);
			}
			else {
				char **args = malloc(sizeof(char *) * (tokens_get_length(tokens) + 1));
				for (size_t i = 0; i < tokens_get_length(tokens); i++) {
					args[i] = tokens_get_token(tokens, i);
				}
				args[tokens_get_length(tokens)] = NULL;
				execv(pathname, args);
				perror(pathname);
				return 1;
			}
		}
		else if (tokens_get_length(tokens) == 0);
		else {
			fprintf(stderr, "%s: command not found\n", tokens_get_token(tokens, 0));
		}

		if (shell_is_interactive)
			/* Please only print shell prompts when standard input is not a tty */
			fprintf(stdout, "%d: ", ++line_num);

		/* Clean up memory */
		tokens_destroy(tokens);
	}

	return 0;
}
