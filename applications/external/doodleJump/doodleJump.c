#include <stdlib.h>
#include <string.h>
#include <dolphin/dolphin.h>

#include <doodleJump_icons.h>
#include <furi.h>
#include <gui/gui.h>
#include <gui/icon_animation_i.h>
#include <input/input.h>

#define TAG "DoodleJump"
#define DEBUG false

#define DOODLE_HEIGHT 15
#define DOODLE_WIDTH 10

#define PLATFORM_MAX 6
#define PLATFORM_DIST 35

#define GRAVITY_JUMP -1.1
#define GRAVITY_TICK 0.15

#define FLIPPER_LCD_WIDTH 128
#define FLIPPER_LCD_HEIGHT 64

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    float gravity;
    Point point;
    IconAnimation* sprite;
    float velocity_x;
    int direction_x;
} Doodle;

typedef struct {
    Point point;
    int width;
    bool visible;
} Platform;

typedef enum {
    GameStateLife,
    GameStateGameOver,
} State;

typedef struct {
    Doodle doodle;
    int score;
    int platform_count;
    Platform platforms[PLATFORM_MAX];
    bool debug;
    State state;
    FuriMutex* mutex;
} GameState;

typedef struct {
    EventType type;
    InputEvent input;
} GameEvent;

static void doodle_jump_random_platform(GameState* game_state) {
    Platform platform;
    platform.visible = true;
    platform.width = rand() % 50 + 30;
    platform.point.y = 0;
    platform.point.x = rand() % (FLIPPER_LCD_WIDTH - platform.width);
    game_state->platform_count++;
    game_state->platforms[game_state->platform_count % PLATFORM_MAX] = platform;
}

static void doodle_jump_game_state_init(GameState* game_state) {
    Doodle doodle;
    doodle.gravity = 0.0f;
    doodle.point.x = 15;
    doodle.point.y = 32;
    doodle.sprite = icon_animation_alloc(&A_doodle);
    doodle.velocity_x = 0.0f;
    doodle.direction_x = 0;

    game_state->debug = DEBUG;
    game_state->doodle = doodle;
    game_state->platform_count = 0;
    game_state->score = 0;
    game_state->state = GameStateLife;
    memset(game_state->platforms, 0, sizeof(game_state->platforms));

    doodle_jump_random_platform(game_state);
}

static void doodle_jump_game_state_free(GameState* game_state) {
    icon_animation_free(game_state->doodle.sprite);
}

static void doodle_jump_game_tick(void* context) {
    GameState* game_state = (GameState*)context;

    if(game_state->state == GameStateLife) {
        if(!game_state->debug) {
            game_state->doodle.gravity += GRAVITY_TICK;
            game_state->doodle.point.y += game_state->doodle.gravity;
        }

        // Checking the location of the last respawned platform.
        Platform* platform = &game_state->platforms[game_state->platform_count % PLATFORM_MAX];
        if(platform->point.y >= FLIPPER_LCD_HEIGHT) {
            doodle_jump_random_platform(game_state);
            game_state->score++;
        }

        // Checking for collisions between doodle and platforms.
        if(game_state->doodle.gravity > 0) {
            for(int i = 0; i < PLATFORM_MAX; i++) {
                platform = &game_state->platforms[i];
                if(platform->visible && platform->point.y > 0 &&
                   game_state->doodle.point.y + DOODLE_HEIGHT >= platform->point.y &&
                   game_state->doodle.point.y + DOODLE_HEIGHT <= platform->point.y + 5 &&
                   game_state->doodle.point.x + DOODLE_WIDTH >= platform->point.x &&
                   game_state->doodle.point.x <= platform->point.x + platform->width) {
                    game_state->doodle.gravity = GRAVITY_JUMP;
                    break; // Exit the loop after finding a collision
                }
            }
        }

        // Checking if the doodle falls off the screen.
        if(game_state->doodle.point.y >= FLIPPER_LCD_HEIGHT - DOODLE_HEIGHT) {
            game_state->state = GameStateGameOver;
        }

        // Update doodle's horizontal movement
        game_state->doodle.point.x +=
            game_state->doodle.velocity_x * game_state->doodle.direction_x;

        // Limit doodle's movement within the screen bounds
        if(game_state->doodle.point.x < 0) {
            game_state->doodle.point.x = 0;
        } else if(game_state->doodle.point.x + DOODLE_WIDTH > FLIPPER_LCD_WIDTH) {
            game_state->doodle.point.x = FLIPPER_LCD_WIDTH - DOODLE_WIDTH;
        }
    }
}

static void
    doodle_jump_game_input_callback(InputEvent* input_event, FuriMessageQueue* event_queue) {
    furi_assert(event_queue);

    GameEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void doodle_jump_game_render_callback(Canvas* canvas, void* ctx) {
    furi_assert(ctx);
    const GameState* game_state = (const GameState*)ctx;
    furi_mutex_acquire(game_state->mutex, FuriWaitForever);

    canvas_draw_frame(canvas, 0, 0, FLIPPER_LCD_WIDTH, FLIPPER_LCD_HEIGHT);

    // Draw the doodle
    canvas_draw_icon_animation(
        canvas, game_state->doodle.point.x, game_state->doodle.point.y, game_state->doodle.sprite);

    // Draw the platforms
    for(int i = 0; i < PLATFORM_MAX; i++) {
        const Platform* platform = &game_state->platforms[i];
        if(platform->visible && platform->point.y > 0 && platform->point.y < FLIPPER_LCD_HEIGHT) {
            canvas_draw_frame(canvas, platform->point.x, platform->point.y, platform->width, 2);
        }
    }

    // Draw the score
    canvas_set_font(canvas, FontSecondary);
    char score_str[16];
    snprintf(score_str, sizeof(score_str), "Score: %d", game_state->score);
    canvas_draw_str_aligned(canvas, 100, 12, AlignCenter, AlignBottom, score_str);

    // Show the buffer on the LCD
    furi_mutex_release(game_state->mutex);
}

int32_t doodle_jump_app(void* p) {
    UNUSED(p);
    int32_t return_code = 0;

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(GameEvent));

    GameState* game_state = (GameState*)malloc(sizeof(GameState));
    if(!game_state) {
        FURI_LOG_E(TAG, "cannot allocate memory for GameState\r\n");
        return_code = 255;
        goto free_and_exit;
    }

    doodle_jump_game_state_init(game_state);

    game_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!game_state->mutex) {
        FURI_LOG_E(TAG, "cannot create mutex\r\n");
        free(game_state); // Free the allocated memory before returning
        return_code = 255;
        goto free_and_exit;
    }

    // Set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, doodle_jump_game_render_callback, game_state);
    view_port_input_callback_set(view_port, doodle_jump_game_input_callback, event_queue);

    FuriTimer* timer = furi_timer_alloc(doodle_jump_game_tick, FuriTimerTypePeriodic, event_queue);
    furi_timer_start(timer, furi_kernel_get_tick_frequency() / 25);

    // Open GUI and register view_port
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // Call dolphin deed on game start
    DOLPHIN_DEED(DolphinDeedPluginGameStart);

    GameEvent event;
    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);
        furi_mutex_acquire(game_state->mutex, FuriWaitForever);

        if(event_status == FuriStatusOk) {
            // press events
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyRight:
                        game_state->doodle.velocity_x = 1.0f;
                        game_state->doodle.direction_x = 1;
                        break;
                    case InputKeyLeft:
                        game_state->doodle.velocity_x = 1.0f;
                        game_state->doodle.direction_x = -1;
                        break;
                    case InputKeyOk:
                        if(game_state->state == GameStateGameOver) {
                            doodle_jump_game_state_init(game_state);
                            furi_mutex_release(game_state->mutex);
                            continue;
                        }
                        break;
                    case InputKeyBack:
                        processing = false;
                        break;
                    default:
                        break;
                    }
                } else if(event.input.type == InputTypeRelease) {
                    switch(event.input.key) {
                    case InputKeyRight:
                    case InputKeyLeft:
                        game_state->doodle.velocity_x = 0.0f;
                        game_state->doodle.direction_x = 0;
                        break;
                    default:
                        break;
                    }
                }
            } else if(event.type == EventTypeTick) {
                doodle_jump_game_tick(game_state);
            }
        }

        view_port_update(view_port);
        furi_mutex_release(game_state->mutex);
    }

    furi_timer_free(timer);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_mutex_free(game_state->mutex);

free_and_exit:
    doodle_jump_game_state_free(game_state);
    furi_message_queue_free(event_queue);
    free(game_state);

    return return_code;
}
