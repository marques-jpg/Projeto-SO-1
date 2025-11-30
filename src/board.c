#include "board.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>  // Para open, O_RDONLY
#include <ctype.h>  // Para isspace, isdigit
#include <string.h> // Para strcmp


FILE * debugfile;

// Helper private function to find and kill pacman at specific position
static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

// Helper private function for getting board position index
static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

// Helper private function for checking valid position
static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height); // Inside of the board boundaries
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int move_pacman(board_t* board, int pacman_index, command_t* command) {
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
        return DEAD_PACMAN; // Invalid or dead pacman
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    // check passo
    if (pac->waiting > 0) {
        pac->waiting -= 1;
        return VALID_MOVE;        
    }
    pac->waiting = pac->passo;

    char direction = command->command;

    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    pac->current_move+=1;

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    char target_content = board->board[new_index].content;

    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        return REACHED_PORTAL;
    }

    // Check for walls
    if (target_content == 'W') {
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        return DEAD_PACMAN;
    }

    // Collect points
    if (board->board[new_index].has_dot) {
        pac->points++;
        board->board[new_index].has_dot = 0;
    }

    board->board[old_index].content = ' ';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->board[new_index].content = 'P';

    return VALID_MOVE;
}

// Helper private function for charged ghost movement in one direction
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    *new_x = x;
    *new_y = y;
    
    switch (direction) {
        case 'W': // Up
            if (y == 0) return INVALID_MOVE;
            *new_y = 0; // In case there is no colision
            for (int i = y - 1; i >= 0; i--) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i + 1; // stop before colision
                    return VALID_MOVE;
                }
                else if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'S': // Down
            if (y == board->height - 1) return INVALID_MOVE;
            *new_y = board->height - 1; // In case there is no colision
            for (int i = y + 1; i < board->height; i++) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'A': // Left
            if (x == 0) return INVALID_MOVE;
            *new_x = 0; // In case there is no colision
            for (int j = x - 1; j >= 0; j--) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j + 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'D': // Right
            if (x == board->width - 1) return INVALID_MOVE;
            *new_x = board->width - 1; // In case there is no colision
            for (int j = x + 1; j < board->width; j++) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;
        default:
            debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
            return INVALID_MOVE;
    }
    return VALID_MOVE;
}   

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    int new_x = x;
    int new_y = y;

    ghost->charged = 0; //uncharge
    int result = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    if (result == INVALID_MOVE) {
        debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
        return INVALID_MOVE;
    }

    // Get board indices
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_index = get_board_index(board, new_x, new_y);

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one
    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    // check passo
    if (ghost->waiting > 0) {
        ghost->waiting -= 1;
        return VALID_MOVE;
    }
    ghost->waiting = ghost->passo;

    char direction = command->command;
    
    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'C': // Charge
            ghost->current_move += 1;
            ghost->charged = 1;
            return VALID_MOVE;
        case 'T': // Wait
            if (command->turns_left == 1) {
                ghost->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    ghost->current_move++;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    // Check board position
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    char target_content = board->board[new_index].content;

    // Check for walls and ghosts
    if (target_content == 'W' || target_content == 'M') {
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    // Check for pacman
    if (target_content == 'P') {
        result = find_and_kill_pacman(board, new_x, new_y);
    }

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one

    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;

    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    debug("Killing %d pacman\n\n", pacman_index);
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = pac->pos_y * board->width + pac->pos_x;

    // Remove pacman from the board
    board->board[index].content = ' ';

    // Mark pacman as dead
    pac->alive = 0;
}

// Static Loading
int load_pacman(board_t* board, int points) {
    board->board[1 * board->width + 1].content = 'P'; // Pacman
    board->pacmans[0].pos_x = 1;
    board->pacmans[0].pos_y = 1;
    board->pacmans[0].alive = 1;
    board->pacmans[0].points = points;
    return 0;
}



// Static Loading
int load_ghost(board_t* board) {
    // Ghost 0
    board->board[3 * board->width + 1].content = 'M'; // Monster
    board->ghosts[0].pos_x = 1;
    board->ghosts[0].pos_y = 3;
    board->ghosts[0].passo = 0;
    board->ghosts[0].waiting = 0;
    board->ghosts[0].current_move = 0;
    board->ghosts[0].n_moves = 16;
    for (int i = 0; i < 8; i++) {
        board->ghosts[0].moves[i].command = 'D';
        board->ghosts[0].moves[i].turns = 1; 
    }
    for (int i = 8; i < 16; i++) {
        board->ghosts[0].moves[i].command = 'A';
        board->ghosts[0].moves[i].turns = 1; 
    }

    // Ghost 1
    board->board[2 * board->width + 4].content = 'M'; // Monster
    board->ghosts[1].pos_x = 4;
    board->ghosts[1].pos_y = 2;
    board->ghosts[1].passo = 1;
    board->ghosts[1].waiting = 1;
    board->ghosts[1].current_move = 0;
    board->ghosts[1].n_moves = 1;
    board->ghosts[1].moves[0].command = 'R'; // Random
    board->ghosts[1].moves[0].turns = 1; 
    
    return 0;
}

int load_level(board_t *board, int points) {
    board->height = 5;
    board->width = 10;
    board->tempo = 10;

    board->n_ghosts = 2;
    board->n_pacmans = 1;

    board->board = calloc(board->width * board->height, sizeof(board_pos_t));
    board->pacmans = calloc(board->n_pacmans, sizeof(pacman_t));
    board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));

    sprintf(board->level_name, "Static Level");

    for (int i = 0; i < board->height; i++) {
        for (int j = 0; j < board->width; j++) {
            if (i == 0 || j == 0 || j == (board->width - 1)) {
                board->board[i * board->width + j].content = 'W';
            }
            else if (i == 4 && j == 8) {
                board->board[i * board->width + j].content = ' ';
                board->board[i * board->width + j].has_portal = 1;
            }
            else {
                board->board[i * board->width + j].content = ' ';
                board->board[i * board->width + j].has_dot = 1;
            }
        }
    }

    load_ghost(board);
    load_pacman(board, points);

    return 0;
}

char* read_file_content(const char* filename) {
    // Imprime o que está a tentar abrir para debug
    // debug("Tentando abrir: [%s]\n", filename); 
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        // ISTO É O IMPORTANTE: Vai escrever no terminal por que razão falhou
        fprintf(stderr, "ERRO CRÍTICO: Não foi possível abrir o ficheiro '%s'.\n", filename);
        perror("Detalhe do erro"); 
        return NULL; 
    }
    
    // Alocação segura
    char *buffer = calloc(4096, sizeof(char)); 
    if (!buffer) { 
        close(fd); 
        return NULL; 
    }

    int bytes_lidos = read(fd, buffer, 4095);
    if (bytes_lidos <= 0) { 
        fprintf(stderr, "AVISO: Ficheiro '%s' está vazio ou erro na leitura.\n", filename);
        free(buffer); 
        close(fd); 
        return NULL; 
    }

    buffer[bytes_lidos] = '\0';
    close(fd); 
    return buffer;
}

int is_valid_pos(board_t *board, int x, int y) {
    if (!board || !board->board) return 0;

    if (!is_valid_position(board, x, y)) return 0;

    int idx = y * board->width + x;
    char pos = board->board[idx].content;

    if (pos == 'W' || pos == 'M' || pos == 'P') return 0;

    return 1;
}

int parse_move_line(char *linha, command_t *moves_array, int *n_moves) {
    if (*n_moves >= MAX_MOVES) return 0;

    char cmd = '\0';
    int turns = 1;

    while(isspace((unsigned char)*linha)) linha++;
    if (*linha == '\0') return 0;

    if (linha[0] == 'T') {
        cmd = 'T';
        if (sscanf(linha + 1, "%d", &turns) != 1) {
            turns = 1;
        }
    } 
    else {
        cmd = linha[0];
    }

    if (cmd != '\0') {
        moves_array[*n_moves].command = cmd;
        moves_array[*n_moves].turns = turns;
        moves_array[*n_moves].turns_left = turns;
        (*n_moves)++;
        return 1;
    }
    return 0;
}

int load_pacman_filename(board_t *board, const char* filename, int index, int points){
    char *buffer = read_file_content(filename);


    if (!buffer) {
        load_pacman(board, points);
        board->pacmans[index].n_moves = 0; 
        return 0;
    }

    pacman_t *p = &board->pacmans[index];
    p->alive = 1; 
    p->points = points;
    p->n_moves = 0;
    p->current_move = 0;
    p->waiting = 0;
    p->passo = 0;

    char *saveptr; 
    char *linha = strtok_r(buffer, "\n", &saveptr);

    while(linha != NULL){

        if(linha[0] != '#'){

            if(strncmp(linha, "PASSO", 5) == 0){
                int passo;
                if(sscanf(linha, "PASSO %d", &passo) == 1){
                    p->passo = passo;
                    p->waiting = passo;
                }
            }

            else if (strncmp(linha, "POS", 3) == 0){
                int l, c;
                if(sscanf(linha, "POS %d %d", &l, &c) == 2){
                    p->pos_y = l;
                    p->pos_x = c;

                    if(is_valid_pos(board, c, l)){
                        int idx = l * board->width + c;
                        board->board[idx].content = 'P';
                        board->board[idx].has_dot = 0;
                    }
                }
            }
            else {
                parse_move_line(linha, p->moves, &p->n_moves);
            }
        }
        linha = strtok_r(NULL, "\n", &saveptr);
    }
    
    free(buffer);
    return 0;
}


int load_ghost_filename(board_t *board, const char* filename, int index){
    char *buffer = read_file_content(filename);
    
    ghost_t *g = &board->ghosts[index];

    g->n_moves = 0;
    g->current_move = 0;
    g->waiting = 0;
    g->charged = 0;
    g->passo = 0;


    char *saveptr; 
    char *linha = strtok_r(buffer, "\n", &saveptr);

    while(linha != NULL){
        if(linha[0] != '#'){

            if(strncmp(linha, "PASSO", 5) == 0){
                int passo;
                if(sscanf(linha, "PASSO %d", &passo) == 1){
                    g->passo = passo;
                    g->waiting = passo;
                }
            }
            else if (strncmp(linha, "POS", 3) == 0){
                int l, c;
                if(sscanf(linha, "POS %d %d", &l, &c) == 2){
                    g->pos_y = l;
                    g->pos_x = c;
                    if(is_valid_pos(board, c, l)){
                        board->board[l * board->width + c].content = 'M';
                    }
                }
            }
            else {
                parse_move_line(linha, g->moves, &g->n_moves);
            }
        }
        linha = strtok_r(NULL, "\n", &saveptr);
    }
    free(buffer);
    return 0;
}


void processar_entidades(board_t *board, char *linha, int tipo, int points) {
    char temp_name[50];
    int offset = 0;
    int count = 0;
    char *cursor = linha + 3; 

    while (sscanf(cursor, "%s%n", temp_name, &offset) == 1) {
        count++;
        cursor += offset;
    }
    if (tipo == 0) {
        board->n_pacmans = count;
        board->pacmans = calloc(count, sizeof(pacman_t));
    } else {
        board->n_ghosts = count;
        board->ghosts = calloc(count, sizeof(ghost_t));
    }

    cursor = linha + 3;
    int i = 0;
    
    while (sscanf(cursor, "%s%n", temp_name, &offset) == 1) {
        
        if (tipo == 0) {
            load_pacman_filename(board, temp_name, i, points); 
        } else {
            load_ghost_filename(board, temp_name, i);
        }
        
        cursor += offset;
        i++;
    }
}

int load_level_filename(board_t *board, const char *filename, int points) {
    char *buffer = read_file_content(filename);
    if (!buffer) return 1;

    char *saveptr;
    char *linha = strtok_r(buffer, "\n", &saveptr);

    int current_row = 0; 

    while (linha != NULL) {
        
        if (linha[0] != '#') {
            
            if (strncmp(linha, "DIM", 3) == 0) {
                int h, w;
                if (sscanf(linha, "DIM %d %d", &h, &w) == 2) {
                    board->height = h;
                    board->width = w;
                    board->board = calloc(board->width * board->height, sizeof(board_pos_t));
                }
            } 
            else if (strncmp(linha, "TEMPO", 5) == 0) {
                int t;
                if (sscanf(linha, "TEMPO %d", &t) == 1) {board->tempo = t; }}

            else if(strncmp(linha, "PAC", 3) == 0){processar_entidades(board, linha, 0, points);}

            else if (strncmp(linha, "MON", 3) == 0) {processar_entidades(board, linha, 1, 0);}

            else {
                if (board->board != NULL && current_row < board->height) {
                    
                    for (int x = 0; x < board->width && linha[x] != '\0'; x++) {
                        
                        int index = (current_row * board->width) + x;
                        char char_lido = linha[x];
                        
                        char conteudo_atual = board->board[index].content;

                        if (char_lido == 'X') {
                            board->board[index].content = 'W'; 
                        } 
                        else if (char_lido == '@') {
                            if (conteudo_atual != 'P' && conteudo_atual != 'M')
                                board->board[index].content = ' ';
                            board->board[index].has_portal = 1;
                        } 
                        else {
                            if (conteudo_atual != 'P' && conteudo_atual != 'M') {
                                board->board[index].content = ' '; 
                            }
                            board->board[index].has_dot = 1;
                        }
                    }
                    current_row++; 
                }
            }
        }
        linha = strtok_r(NULL, "\n", &saveptr);
    }
    free(buffer);
    return 0;
}
   

void unload_level(board_t * board) {
    free(board->board);
    free(board->pacmans);
    free(board->ghosts);
}

void open_debug_file(char *filename) {
    debugfile = fopen(filename, "w");
}

void close_debug_file() {
    fclose(debugfile);
}

void debug(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(debugfile, format, args);
    va_end(args);

    fflush(debugfile);
}

void print_board(board_t *board) {
    if (!board || !board->board) {
        debug("[%d] Board is empty or not initialized.\n", getpid());
        return;
    }

    // Large buffer to accumulate the whole output
    char buffer[8192];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "=== [%d] LEVEL INFO ===\n"
                       "Dimensions: %d x %d\n"
                       "Tempo: %d\n"
                       "Pacman file: %s\n",
                       getpid(), board->height, board->width, board->tempo, board->pacman_file);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "Monster files (%d):\n", board->n_ghosts);

    for (int i = 0; i < board->n_ghosts; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "  - %s\n", board->ghosts_files[i]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            if (offset < sizeof(buffer) - 2) {
                buffer[offset++] = board->board[idx].content;
            }
        }
        if (offset < sizeof(buffer) - 2) {
            buffer[offset++] = '\n';
        }
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "==================\n");

    buffer[offset] = '\0';

    debug("%s", buffer);
}
