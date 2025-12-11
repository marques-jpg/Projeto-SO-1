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

typedef struct {
    board_t *board;
    pthread_mutex_t mutex;
    pthread_cond_t input_cond;
    int running;
    int outcome;
    char pending_input;
    int save_request;
} game_state_t;

typedef struct {
    game_state_t *state;
    int ghost_index;
} ghost_thread_args_t;

static void set_outcome(game_state_t *state, int outcome) {
    if (state->outcome == CONTINUE_PLAY) {
        state->outcome = outcome;
    }
    state->running = 0;
    pthread_cond_broadcast(&state->input_cond);
}

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
            state->pending_input = input; // latest command wins
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

static void *pacman_thread(void *arg) {
    game_state_t *state = (game_state_t *)arg;
    board_t *board = state->board;

    while (1) {
        pthread_mutex_lock(&state->mutex);
        if (!state->running) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }

        pacman_t *pacman = &board->pacmans[0];
        command_t cmd;

        if (pacman->n_moves == 0) {
            while (state->pending_input == '\0' && state->running) {
                pthread_cond_wait(&state->input_cond, &state->mutex);
            }
            if (!state->running) {
                pthread_mutex_unlock(&state->mutex);
                break;
            }
            cmd = build_manual_command(state->pending_input);
            state->pending_input = '\0';
        } else {
            cmd = pacman->moves[pacman->current_move % pacman->n_moves];
        }

        pthread_mutex_unlock(&state->mutex);

        if (cmd.command == 'Q') {
            pthread_mutex_lock(&state->mutex);
            set_outcome(state, QUIT_GAME);
            pthread_mutex_unlock(&state->mutex);
            continue;
        }

        if (cmd.command == 'G') {
            pthread_mutex_lock(&state->mutex);
            if (board->save_active == 0) {
                board->save_active = 1;
                state->save_request = 1;
                set_outcome(state, CONTINUE_PLAY); // signal threads to stop so main can fork
            }
            pthread_mutex_unlock(&state->mutex);
            continue;
        }

        pthread_mutex_lock(&state->mutex);
        if (!state->running) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }
        int result = move_pacman(board, 0, &cmd);
        if (result == REACHED_PORTAL) {
            set_outcome(state, NEXT_LEVEL);
        } else if (result == DEAD_PACMAN) {
            set_outcome(state, QUIT_GAME);
        }
        pthread_mutex_unlock(&state->mutex);

        if (board->tempo != 0) {
            sleep_ms(board->tempo);
        }
    }

    return NULL;
}

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

        // CORREÇÃO: Calcular o índice do comando em vez de fazer uma cópia da estrutura
        int cmd_index = ghost->current_move % ghost->n_moves;
        
        pthread_mutex_unlock(&state->mutex);

        pthread_mutex_lock(&state->mutex);
        if (!state->running) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }

        // CORREÇÃO: Obter um ponteiro para o comando real dentro do array
        // Isto garante que quando o move_ghost altera o 'turns_left', a alteração fica guardada.
        ghost = &board->ghosts[ghost_index];
        command_t *cmd_ptr = &ghost->moves[cmd_index];

        int result = move_ghost(board, ghost_index, cmd_ptr);
        
        if (result == DEAD_PACMAN) {
            set_outcome(state, QUIT_GAME);
        }
        pthread_mutex_unlock(&state->mutex);

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

int encontrar_niveis(const char *dirpath, char lista[MAX_LEVELS][MAX_FILENAME]) {
    DIR *dirp = opendir(dirpath);
    if (dirp == NULL) {
        perror("Erro ao abrir diretoria");
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

int main(int argc, char** argv) {
    if (argc != 2) {    
        fprintf(stderr, "Usage: %s <level_directory>\n", argv[0]);
        return 1;
    }

    if (chdir(argv[1]) != 0) {
        perror("Erro ao entrar na diretoria");
        return 1;
    }

    srand((unsigned int)time(NULL));
    open_debug_file("debug.log");
    terminal_init();
    
    char lista_niveis[MAX_LEVELS][MAX_FILENAME];
    int n_niveis = encontrar_niveis(".", lista_niveis);

    int accumulated_points = 0;
    bool game_over = false;

    int global_save_active = 0;

    for (int i = 0; i < n_niveis; i++) {
        if (game_over) break;

        board_t game_board = {0};

        game_board.save_active = global_save_active;

        if (load_level_filename(&game_board, lista_niveis[i], accumulated_points) != 0) {
             debug("Falha ao carregar nivel: %s\n", lista_niveis[i]);
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

            pthread_create(&render_tid, NULL, render_thread, &state);
            pthread_create(&pacman_tid, NULL, pacman_thread, &state);

            for (int g = 0; g < game_board.n_ghosts; g++) {
                ghost_args[g].state = &state;
                ghost_args[g].ghost_index = g;
                pthread_create(&ghost_tids[g], NULL, ghost_thread, &ghost_args[g]);
            }

            pthread_join(pacman_tid, NULL);
            for (int g = 0; g < game_board.n_ghosts; g++) {
                pthread_join(ghost_tids[g], NULL);
            }
            pthread_join(render_tid, NULL);

            pthread_mutex_destroy(&state.mutex);
            pthread_cond_destroy(&state.input_cond);

            if (state.save_request) {
                terminal_cleanup();
                pid_t pid = fork();

                if (pid < 0) {
                    perror("Erro ao guardar o jogo");
                    game_board.save_active = 0;
                    continue;
                } else if (pid > 0) {
                    int status;
                    while (1) {
                        waitpid(pid, &status, 0);

                        if (WIFEXITED(status)) {
                            if (WEXITSTATUS(status) == 67) {
                                pid_t new_pid = fork();
                                if (new_pid == 0) {
                                    break;
                                } else if (new_pid > 0) {
                                    pid = new_pid;
                                } else {
                                    perror("Erro ao restaurar jogo");
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
                    // child keeps playing: keep save_active so death exits with 67, reinit ncurses, and re-loop
                    terminal_init();
                    game_board.save_active = 1;
                    repeat_level = 1;
                    continue;
                }
            }

            if (game_board.save_active) {
                global_save_active = 1;
            }

            if (state.outcome == NEXT_LEVEL) {
                sleep_ms(game_board.tempo);
            } else if (state.outcome == QUIT_GAME) {
                sleep_ms(2000);
                game_over = true;

                if (game_board.save_active) {
                    if (!game_board.pacmans[0].alive) {
                        close_debug_file();
                        exit(67);
                    } else {
                        terminal_cleanup();
                        close_debug_file();
                        exit(0);
                    }
                }
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