#include <stdint.h> 
#include <stdlib.h>
#include <stdio.h>

#define PIXEL_BUFFER_BASE 0xC8000000
#define CHAR_BUFFER_BASE  0xC9000000
#define PS2_DATA_ADDR     0xFF200100
#define TIMER_BASE        0xFFFEC600

// Colors
#define COLOR_LIGHTBLUE   0x867F
#define COLOR_BLACK       0x0000
#define COLOR_PINK        0xF81F
#define COLOR_GREY        0x8410
#define COLOR_DARKBLUE    0x000F

// Game states
#define GAME_INIT         0
#define GAME_RUNNING      1
#define GAME_OVER         2

// PS2 keyboard scancodes
#define PS2_KEY_S         0x1B
#define PS2_KEY_LEFT      0x6B
#define PS2_KEY_RIGHT     0x74

// Game variables
int game_state;
int player_x;
int player_y;
int score;
float speed;

typedef struct {
    int x;
    int y;
    int active;
} TrafficCar;

TrafficCar traffic_cars[3];

// -------------------- Pseudo Number Generator --------------------

unsigned int seed = 12345;  

// Function to generate a pseudo-random number
unsigned int pseudo_random() {
    // LCG parameters (from Numerical Recipes)
    seed = (1103515245 * seed + 12345) & 0x7fffffff;
    return seed;
}

// Function to get a pseudo-random number within a specific range [min, max]
unsigned int random_in_range(int min, int max) {
    return (pseudo_random() % (max - min + 1)) + min;
}

// -------------------- VGA Functions --------------------

void VGA_draw_point(int x, int y, short c) {
    volatile short *pixel_addr = (volatile short *)(PIXEL_BUFFER_BASE + (y << 10) + (x << 1));
    *pixel_addr = c;
}

void VGA_write_char(int x, int y, char c) {
    // Check for valid coordinates
    if (x < 0 || x >= 80 || y < 0 || y >= 60) {
        return; 
    }
    volatile char *char_addr = (volatile char *)(CHAR_BUFFER_BASE + (y << 7) + x);
    *char_addr = c;
}

void VGA_clear_pixelbuff() {
    int x, y;
    for (y = 0; y < 240; y++) {
        for (x = 0; x < 320; x++) {
            VGA_draw_point(x, y, 0);
        }
    }
}

 void VGA_clear_charbuff() {
    int x, y;
    for (y = 0; y < 60; y++) {
        for (x = 0; x < 80; x++) {
            VGA_write_char(x, y, 0);
        }
    }
}

// -------------------- PS2 Keyboard Functions --------------------

int read_PS2_data(char *data) {
    volatile int *ps2_data = (volatile int *)PS2_DATA_ADDR;
    int value = *ps2_data;
    int valid = (value >> 15) & 0x1;

    if (valid) {
        *data = (char)(value & 0xFF);
        return 1;
    }

    return 0;
}

// -------------------- Timer Functions --------------------

#define TIMER_BASE         0xFFFEC600
#define TIMER_LOAD         ((volatile int *)(TIMER_BASE + 0x00))
#define TIMER_VALUE        ((volatile int *)(TIMER_BASE + 0x04))
#define TIMER_CONTROL      ((volatile int *)(TIMER_BASE + 0x08))
#define TIMER_STATUS       ((volatile int *)(TIMER_BASE + 0x0C))

void init_timer(int count_value) {
    *TIMER_LOAD = count_value;
    *TIMER_CONTROL = (1 << 0) |  // Enable timer
                     (1 << 1) |  // Auto-reload
                     (1 << 2);   // Enable interrupt bit 
}

int timer_expired() {
    if (*TIMER_STATUS & 1) {
        *TIMER_STATUS = 1;  
        return 1;
    }
    return 0;
}

// -------------------- Game Drawing Functions --------------------

void draw_road() {
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 10; x++) {
            VGA_draw_point(x, y, COLOR_GREY);
        }
    }
    for (int y = 0; y < 240; y++) {
        for (int x = 10; x < 90; x++) {
            VGA_draw_point(x, y, COLOR_LIGHTBLUE);
        }
    }
    for (int y = 0; y < 240; y++) {
        for (int x = 90; x < 100; x++) {
            VGA_draw_point(x, y, COLOR_GREY);
        }
    }
}

void draw_car(int x, int y, short color) {
    int i, j;
    for (i = 0; i < 48; i++) {
        for (j = 0; j < 30; j++) {
            int draw_x = x - 15 + j;
            int draw_y = y - 24 + i;

            if (draw_x >= 0 && draw_x < 320 && draw_y >= 0 && draw_y < 240) {
                VGA_draw_point(draw_x, draw_y, color);
            }
        }
    }
}

void erase_car(int x, int y) {
    draw_car(x, y, COLOR_LIGHTBLUE);
}

// -------------------- Game Logic Functions --------------------

void init_game() {
    game_state = GAME_INIT;

    player_x = 35;
    player_y = 216;

    score = 0;
    speed = 10.0;
    
    for (int i = 0; i < 3; i++) {
        traffic_cars[i].active = 0;
    }

    VGA_clear_pixelbuff();
    VGA_clear_charbuff();

    draw_road();
    draw_car(player_x, player_y, COLOR_DARKBLUE);

    char startMsg[] = "Press S to start";
    for (int i = 0; startMsg[i] != '\0'; i++) {
        VGA_write_char(40 + i, 20, startMsg[i]);
    }
}

void spawn_object() {  
    int spawn = 1;

    int topmost_y = 240;

    for (int i = 0; i < 3; i++) {
        if (traffic_cars[i].active && traffic_cars[i].y < topmost_y) {
            topmost_y = traffic_cars[i].y;
        }
    }

    if (topmost_y < 120) {
        spawn = 0;
    }

    if (spawn) {
        for (int i = 0; i < 3; i++) {
            if (!traffic_cars[i].active) {
                int lane = random_in_range(1, 5) % 2;
                traffic_cars[i].x = 35 + (lane * 30);
                traffic_cars[i].y = -24;
                traffic_cars[i].active = 1;

                draw_car(traffic_cars[i].x, traffic_cars[i].y, COLOR_PINK);
                break;
            }
        }
    }
}

void update_objects() {
    int point_gained = 0;

    for (int i = 0; i < 3; i++) {
        if (traffic_cars[i].active) {
            int old_y = traffic_cars[i].y;
            int delta = (int)speed;
            traffic_cars[i].y += delta;

            // Check if the car has completely left the screen
            if ((traffic_cars[i].y - 24) >= 240) {
                erase_car(traffic_cars[i].x, old_y);
                traffic_cars[i].active = 0;
                point_gained = 1;
            } 

            // Full erase and redraw
            else if (delta >= 48) {
                erase_car(traffic_cars[i].x, old_y);
                draw_car(traffic_cars[i].x, traffic_cars[i].y, COLOR_PINK);
            } 
            
            // Line-by-line update
            else {
                for (int j = 0; j < delta; j++) {
                    int erase_y = old_y - 24 + j;
                    for (int x = traffic_cars[i].x - 15; x < traffic_cars[i].x + 15; x++) {
                        if (x >= 0 && x < 320 && erase_y >= 0 && erase_y < 240) {
                            VGA_draw_point(x, erase_y, COLOR_LIGHTBLUE);
                        }
                    }
                }

                for (int j = 0; j < delta; j++) {
                    int draw_y = traffic_cars[i].y + 23 - j;
                    for (int x = traffic_cars[i].x - 15; x < traffic_cars[i].x + 15; x++) {
                        if (x >= 0 && x < 320 && draw_y >= 0 && draw_y < 240)
                            VGA_draw_point(x, draw_y, COLOR_PINK);
                    }
                }
            }
        }
    }

    if (point_gained) {
        score++;
        speed *= 1.01;

        char scoreStr[10];
        sprintf(scoreStr, "Score: %d", score);

        for (int j = 0; j < 10; j++) {
            VGA_write_char(40 + j, 20, ' ');
        }

        for (int j = 0; scoreStr[j] != '\0'; j++) {
            VGA_write_char(40 + j, 20, scoreStr[j]);
        }
    }
}

void update_character_position(int direction) {
    erase_car(player_x, player_y);

    if (direction == 0) {
        player_x = 35;
    } else {
        player_x = 65;
    }

    draw_car(player_x, player_y, COLOR_DARKBLUE);
}

int check_collision() {
    for (int i = 0; i < 3; i++) {
        if (traffic_cars[i].active) {
            if (abs(traffic_cars[i].x - player_x) < 30 && abs(traffic_cars[i].y - player_y) < 48) {
                return 1;
            }
        }
    }

    return 0;
}

void game_over() {
    game_state = GAME_OVER;

    char gameOverMsg[] = "Game Over!";
    char scoreMsg[20];
    sprintf(scoreMsg, "Final Score: %d", score);
    char restartMsg[] = "Press S to start";

    for (int i = 0; i < 20; i++) {
        VGA_write_char(40 + i, 20, ' ');
        VGA_write_char(40 + i, 22, ' ');
        VGA_write_char(40 + i, 26, ' ');
    }

    for (int i = 0; gameOverMsg[i] != '\0'; i++) {
        VGA_write_char(40 + i, 20, gameOverMsg[i]);
    }
    for (int i = 0; scoreMsg[i] != '\0'; i++) {
        VGA_write_char(40 + i, 22, scoreMsg[i]);
    }
    for (int i = 0; restartMsg[i] != '\0'; i++) {
        VGA_write_char(40 + i, 26, restartMsg[i]);
    }
}

void process_input() {
    char data;

    if (read_PS2_data(&data)) {
        if (game_state == GAME_INIT || game_state == GAME_OVER) {
            if (data == 0x1B) {
                init_game(); 
                game_state = GAME_RUNNING;

                for (int y = 20; y < 27; y++) {
                    for (int x = 40; x < 60; x++) {
                        VGA_write_char(x, y, ' ');
                    }
                }

                char scoreStr[10];
                sprintf(scoreStr, "Score: %d", score);

                for (int j = 0; scoreStr[j] != '\0'; j++) {
                    VGA_write_char(40 + j, 20, scoreStr[j]);
                }
            }
        } else if (game_state == GAME_RUNNING) {
            if (data == 0x6B) {
                update_character_position(0);
            } else if (data == 0x74) {
                update_character_position(1);
            }
        }
    }
}

int main() {
    init_game();
    init_timer(50000000);

    while (1) {
        process_input();

        if (game_state == GAME_RUNNING) {
            if (timer_expired()) {
                spawn_object();
                update_objects();

                if (check_collision()) {
                    game_over();
                }

                init_timer(50000000);
            }
        }
    }

    return 0;
}