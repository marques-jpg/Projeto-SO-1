#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2

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

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play;

    // Lógica PACMAN
    if (pacman->n_moves == 0) { // Controlo manual
        command_t c; 
        c.command = get_input();
        if(c.command == '\0') return CONTINUE_PLAY;
        c.turns = 1;
        play = &c;
    }
    else { // Controlo automático (ficheiro)
        // O módulo (%) garante o loop infinito dos comandos lidos
        play = &pacman->moves[pacman->current_move % pacman->n_moves];
    }

    if (play->command == 'Q') return QUIT_GAME;

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) return NEXT_LEVEL;
    if (result == DEAD_PACMAN) return QUIT_GAME;
    
    // Lógica FANTASMAS
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        
        // Executar apenas se houver movimentos carregados
        if (ghost->n_moves > 0) {
            // O módulo (%) garante o loop infinito dos comandos lidos
            move_ghost(game_board, i, &ghost->moves[ghost->current_move % ghost->n_moves]);
        }
    }

    if (!game_board->pacmans[0].alive) return QUIT_GAME;

    return CONTINUE_PLAY;  
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


    for (int i = 0; i < n_niveis; i++) {
        if (game_over) break;

        board_t game_board = {0};
        

        if (load_level_filename(&game_board, lista_niveis[i], accumulated_points) != 0) {
             debug("Falha ao carregar nivel: %s\n", lista_niveis[i]);
             continue;
        }

        strncpy(game_board.level_name, lista_niveis[i], 255);
        
        bool level_complete = false;
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while(!level_complete && !game_over) {
            int result = play_board(&game_board); 

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                level_complete = true;
            }
            else if(result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(2000);
                game_over = true;
            }
            else {
                screen_refresh(&game_board, DRAW_MENU); 
            }
            accumulated_points = game_board.pacmans[0].points;      
        }
        unload_level(&game_board);
    }    

    terminal_cleanup();
    close_debug_file();

    return 0;
}