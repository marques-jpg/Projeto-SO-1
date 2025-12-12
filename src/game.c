#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2


// Safely updates the game outcome (Win/Loss) and notifies waiting threads
static void set_outcome(game_state_t *state, int outcome) {
    if (state->outcome == CONTINUE_PLAY) {
        state->outcome = outcome;
    }
    state->running = 0;
    pthread_cond_broadcast(&state->input_cond);
}

// Render Thread: Handles screen drawing and captures user input
static void *render_thread(void *arg) {
    game_state_t *state = (game_state_t *)arg;

    while (1) {
        pthread_mutex_lock(&state->mutex);
        int running = state->running;
        int outcome = state->outcome;
        board_t *board = state->board;

        int draw_mode = DRAW_MENU;
        if (outcome == NEXT_LEVEL) {
            draw_mode = DRAW_WIN;
        } else if (outcome == QUIT_GAME) {
            draw_mode = DRAW_GAME_OVER;
        }

        draw_board(board, draw_mode);
        refresh_screen();
        pthread_mutex_unlock(&state->mutex);

        if (!running) break;

        char input = get_input();
        if (input != '\0') {
            pthread_mutex_lock(&state->mutex);
            state->pending_input = input; // Stores input to be used by Pacman thread
            pthread_cond_broadcast(&state->input_cond);
            pthread_mutex_unlock(&state->mutex);
        }

        if (board->tempo != 0) {
            sleep_ms(board->tempo);
        }
    }

    return NULL;
}

static command_t build_manual_command(char input) {
    command_t cmd;
    cmd.command = input;
    cmd.turns = 1;
    cmd.turns_left = 1;
    return cmd;
}

// Pacman Thread: Controls Pacman logic (manual input or auto moves)
static void *pacman_thread(void *arg) {
    game_state_t *state = (game_state_t *)arg;
    board_t *board = state->board;

    command_t manual_cmd; 

    while (1) {
        pthread_mutex_lock(&state->mutex);
        if (!state->running) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }

        pacman_t *pacman = &board->pacmans[0];
        command_t *cmd_ptr;

        // If no predefined moves, wait for user input from Render Thread
        if (pacman->n_moves == 0) {
            while (state->pending_input == '\0' && state->running) {
                pthread_cond_wait(&state->input_cond, &state->mutex);
            }
            if (!state->running) {
                pthread_mutex_unlock(&state->mutex);
                break;
            }
            manual_cmd = build_manual_command(state->pending_input);
            state->pending_input = '\0';
            cmd_ptr = &manual_cmd;
        } else {
            int cmd_index = pacman->current_move % pacman->n_moves;
            cmd_ptr = &pacman->moves[cmd_index];
        }

        pthread_mutex_unlock(&state->mutex);

        if (cmd_ptr->command == 'Q') {
            pthread_mutex_lock(&state->mutex);
            set_outcome(state, QUIT_GAME);
            pthread_mutex_unlock(&state->mutex);
            continue;
        }

        // Handle Quick Save request
        if (cmd_ptr->command == 'G') {
            pthread_mutex_lock(&state->mutex);
            if (board->save_active == 0) {
                board->save_active = 1;
                state->save_request = 1;
                set_outcome(state, CONTINUE_PLAY);
            }
            pthread_mutex_unlock(&state->mutex);
            continue;
        }

        pthread_mutex_lock(&state->mutex);
        int is_running = state->running;
        pthread_mutex_unlock(&state->mutex);
        if (!is_running) break;
        
        int result = move_pacman(board, 0, cmd_ptr); 
        
        if (result == REACHED_PORTAL || result == DEAD_PACMAN) {
            pthread_mutex_lock(&state->mutex);
            if (result == REACHED_PORTAL) {
                set_outcome(state, NEXT_LEVEL);
            } else if (result == DEAD_PACMAN) {
                set_outcome(state, QUIT_GAME);
            }
            pthread_mutex_unlock(&state->mutex);
        }

        if (board->tempo != 0) {
            sleep_ms(board->tempo);
        }
    }

    return NULL;
}

// Ghost Thread: Manages the movement of a single ghost
static void *ghost_thread(void *arg) {
    ghost_thread_args_t *ghost_args = (ghost_thread_args_t *)arg;
    game_state_t *state = ghost_args->state;
    int ghost_index = ghost_args->ghost_index;
    board_t *board = state->board;

    while (1) {
        pthread_mutex_lock(&state->mutex);
        if (!state->running) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }

        ghost_t *ghost = &board->ghosts[ghost_index];
        if (ghost->n_moves == 0) {
            pthread_mutex_unlock(&state->mutex);
            if (board->tempo != 0) {
                sleep_ms(board->tempo);
            }
            continue;
        }

        int cmd_index = ghost->current_move % ghost->n_moves;
        command_t *cmd_ptr = &ghost->moves[cmd_index]; 
        
        pthread_mutex_unlock(&state->mutex);

        pthread_mutex_lock(&state->mutex);
        int is_running = state->running;
        pthread_mutex_unlock(&state->mutex);
        if (!is_running) break;

        int result = move_ghost(board, ghost_index, cmd_ptr);
        
        if (result == DEAD_PACMAN) {
            pthread_mutex_lock(&state->mutex);
            set_outcome(state, QUIT_GAME);
            pthread_mutex_unlock(&state->mutex);
        }

        if (board->tempo != 0) {
            sleep_ms(board->tempo);
        }
    }

    return NULL;
}

int has_extension(const char *filename, const char *ext) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return 0;
    return (strcmp(dot, ext) == 0);
}

// Scans the directory for valid level files (.lvl)
int find_levels(const char *dirpath, char lista[MAX_LEVELS][MAX_FILENAME]) {
    DIR *dirp = opendir(dirpath);
    if (dirp == NULL) {
        perror("Error opening directory");
        return 0;
    }

    struct dirent *dp;
    int count = 0;

    while ((dp = readdir(dirp)) != NULL) {
        
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        if (has_extension(dp->d_name, ".lvl") && count < MAX_LEVELS) {
            strncpy(lista[count], dp->d_name, MAX_FILENAME - 1);
            lista[count][MAX_FILENAME - 1] = '\0';
            count++;
        }
    }
    closedir(dirp);
    
    return count;
}

// Main Game Loop: Handles initialization, level loading, threads, and save/restore
int main(int argc, char** argv) {
    if (argc != 2) {    
        fprintf(stderr, "Usage: %s <level_directory>\n", argv[0]);
        return 1;
    }

    if (chdir(argv[1]) != 0) {
        perror("Error changing directory");
        return 1;
    }

    srand((unsigned int)time(NULL));
    open_debug_file("debug.log");
    terminal_init();
    
    char lista_niveis[MAX_LEVELS][MAX_FILENAME];
    int n_niveis = find_levels(".", lista_niveis);

    int accumulated_points = 0;
    bool game_over = false;

    int global_save_active = 0;

    for (int i = 0; i < n_niveis; i++) {
        if (game_over) break;

        board_t game_board = {0};

        game_board.save_active = global_save_active;

        if (load_level_filename(&game_board, lista_niveis[i], accumulated_points) != 0) {
             debug("Failed to load level: %s\n", lista_niveis[i]);
             continue;
        }

        strncpy(game_board.level_name, lista_niveis[i], 255);
        
        int repeat_level = 1;
        while (repeat_level) {
            repeat_level = 0;

            game_state_t state = {
                .board = &game_board,
                .running = 1,
                .outcome = CONTINUE_PLAY,
                .pending_input = '\0',
                .save_request = 0
            };

            pthread_mutex_init(&state.mutex, NULL);
            pthread_cond_init(&state.input_cond, NULL);

            pthread_t render_tid;
            pthread_t pacman_tid;
            pthread_t ghost_tids[MAX_GHOSTS];
            ghost_thread_args_t ghost_args[MAX_GHOSTS];

            // Start Threads
            pthread_create(&render_tid, NULL, render_thread, &state);
            pthread_create(&pacman_tid, NULL, pacman_thread, &state);

            for (int g = 0; g < game_board.n_ghosts; g++) {
                ghost_args[g].state = &state;
                ghost_args[g].ghost_index = g;
                pthread_create(&ghost_tids[g], NULL, ghost_thread, &ghost_args[g]);
            }

            // Join Threads
            pthread_join(pacman_tid, NULL);
            for (int g = 0; g < game_board.n_ghosts; g++) {
                pthread_join(ghost_tids[g], NULL);
            }
            pthread_join(render_tid, NULL);

            pthread_mutex_destroy(&state.mutex);
            pthread_cond_destroy(&state.input_cond);

            // Handle Save Game Request (Fork logic)
            if (state.save_request) {
                terminal_cleanup();
                pid_t pid = fork();

                if (pid < 0) {
                    perror("Error saving game");
                    game_board.save_active = 0;
                    continue;
                } else if (pid > 0) {
                    // Parent process waits for child
                    int status;
                    while (1) {
                        waitpid(pid, &status, 0);

                        if (WIFEXITED(status)) {
                            // Exit code 67 indicates Pacman died with an active save -> Restore (Fork again)
                            if (WEXITSTATUS(status) == 67) {
                                pid_t new_pid = fork();
                                if (new_pid == 0) {
                                    break; // Child (restored game) breaks loop to continue playing
                                } else if (new_pid > 0) {
                                    pid = new_pid; // Parent updates pid and waits again
                                } else {
                                    perror("Error restoring game");
                                    exit(1);
                                }
                            } else {
                                exit(WEXITSTATUS(status));
                            }
                        } else {
                            exit(1);
                        }
                    }

                    game_board.save_active = 0;
                    repeat_level = 1;
                    continue;
                } else {
                    // Child process continues the game
                    terminal_init();
                    game_board.save_active = 1;
                    repeat_level = 1;
                    continue;
                }
            }

            if (game_board.save_active) {
                global_save_active = 1;
            }

            // Handle level outcome
            if (state.outcome == NEXT_LEVEL) {
                sleep_ms(game_board.tempo);
            } else if (state.outcome == QUIT_GAME) {
                sleep_ms(2000);
                game_over = true;

                if (game_board.save_active) {
                    if (!game_board.pacmans[0].alive) {
                        close_debug_file();
                        exit(67); // Special exit code for "Death with Save"
                    } else {
                        terminal_cleanup();
                        close_debug_file();
                        exit(0);
                    }
                }
            }

            if(i == n_niveis -1 && state.outcome == NEXT_LEVEL){
                draw_board(&game_board, DRAW_WIN);
                refresh_screen();
                sleep_ms(2000);
                game_over=true;
            }else{
                sleep_ms(game_board.tempo);
            }

            repeat_level = 0;
        }

        accumulated_points = game_board.pacmans[0].points;      
        unload_level(&game_board);
    }

    terminal_cleanup();
    close_debug_file();

    return 0;
}