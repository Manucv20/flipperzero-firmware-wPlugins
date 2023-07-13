#include <furi.h>
#include <furi_hal.h>
#include <stdlib.h>
#include <gui/gui.h>
#include <input/input.h>

#include "hello420_icons.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define BONG_ICON_WIDTH 24
#define BONG_ICON_HEIGHT 32
#define WEED_ICON_WIDTH 14
#define WEED_ICON_HEIGHT 14

typedef struct {
    uint8_t x, y;
    int8_t speed_x, speed_y;
    uint8_t prev_x, prev_y;  // Previous position
} ImagePosition;

typedef struct {
    uint8_t x, y;
    uint32_t counter;
} AdditionalImage;

static ImagePosition bong_position = {.x = 0, .y = 0, .speed_x = 0, .speed_y = 0, .prev_x = 0, .prev_y = 0};
static AdditionalImage weed_image = {.x = 0, .y = 0, .counter = 0};

// Screen is 128x64 px
static void app_draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);

    canvas_clear(canvas);

    // Calculate the interpolated position
    float interpolation_factor = 0.5;  // Adjust this value based on your preference
    uint8_t interpolated_x = (uint8_t)(bong_position.prev_x + (bong_position.x - bong_position.prev_x) * interpolation_factor);
    uint8_t interpolated_y = (uint8_t)(bong_position.prev_y + (bong_position.y - bong_position.prev_y) * interpolation_factor);

    canvas_draw_icon(canvas, interpolated_x, interpolated_y, &I_bong);
    canvas_draw_icon(canvas, weed_image.x, weed_image.y, &I_weed);

    // Draw the score
    char score_text[16];
    snprintf(score_text, sizeof(score_text), "Score: %lu", weed_image.counter);
    canvas_draw_str_aligned(canvas, 85, 5, AlignLeft, AlignTop, score_text);

    if (weed_image.counter >= 10) {
        // Show "You Win" screen
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 34, 20, 62, 24);

        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 34, 20, 62, 24);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 37, 31, "You Win!");

        canvas_set_font(canvas, FontSecondary);
        char buffer[12];
        snprintf(buffer, sizeof(buffer), "Score: %lu", (unsigned long)weed_image.counter);
        canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignBottom, buffer);
    }
}

static void app_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = (FuriMessageQueue*)ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static void move_bong_image() {
    // Store the previous position
    bong_position.prev_x = bong_position.x;
    bong_position.prev_y = bong_position.y;

    // Update the position with interpolation
    bong_position.x += bong_position.speed_x;
    bong_position.y += bong_position.speed_y;

    // Wrap around the screen boundaries
    bong_position.x = bong_position.x % SCREEN_WIDTH;
    bong_position.y = bong_position.y % SCREEN_HEIGHT;
}

static void generate_random_weed_position() {
    bool validPosition = false;
    unsigned int seed = (unsigned int)rand();
    srand(seed);

    int maxAttempts = 10; // Maximum number of attempts to generate a valid position

    while (!validPosition && maxAttempts > 0) {
        validPosition = true; // Assume the position is valid unless proven otherwise

        weed_image.x = rand() % (SCREEN_WIDTH - WEED_ICON_WIDTH + 1);
        weed_image.y = rand() % (SCREEN_HEIGHT - WEED_ICON_HEIGHT + 1);

        // Check if the position is too close to the bong image
        if (abs(weed_image.x - bong_position.x) < (BONG_ICON_WIDTH + WEED_ICON_WIDTH) &&
            abs(weed_image.y - bong_position.y) < (BONG_ICON_HEIGHT + WEED_ICON_HEIGHT)) {
            validPosition = false; // Position is too close, not valid
        }

        maxAttempts--;
    }

    if (!validPosition) {
        // Use the previous position if a valid position couldn't be generated within the maximum attempts
        weed_image.x = bong_position.x;
        weed_image.y = bong_position.y;
    }
}

static bool check_overlap() {
    return (weed_image.x <= (bong_position.x + BONG_ICON_WIDTH) &&
            (weed_image.x + WEED_ICON_WIDTH) >= bong_position.x &&
            weed_image.y <= (bong_position.y + BONG_ICON_HEIGHT) &&
            (weed_image.y + WEED_ICON_HEIGHT) >= bong_position.y);
}

int32_t hello420_main(void* p) {
    UNUSED(p);
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, app_draw_callback, view_port);
    view_port_input_callback_set(view_port, app_input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;

    bool running = true;
    bool weedImagePlaced = false;

    generate_random_weed_position(); // Generate initial weed position

    while (running) {
        if (furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if ((event.type == InputTypePress) || (event.type == InputTypeRepeat)) {
                switch (event.key) {
                    case InputKeyLeft:
                        bong_position.speed_x = -2;
                        break;
                    case InputKeyRight:
                        bong_position.speed_x = 2;
                        break;
                    case InputKeyUp:
                        bong_position.speed_y = -2;
                        break;
                    case InputKeyDown:
                        bong_position.speed_y = 2;
                        break;
                    default:
                        running = false;
                        break;
                }
            } else if (event.type == InputTypeRelease) {
                switch (event.key) {
                    case InputKeyLeft:
                    case InputKeyRight:
                        bong_position.speed_x = 0;
                        break;
                    case InputKeyUp:
                    case InputKeyDown:
                        bong_position.speed_y = 0;
                        break;
                    default:
                        break;
                }
            }
        }

        move_bong_image();

        if (check_overlap()) {
            if (!weedImagePlaced) {
                weed_image.counter++;  // Increment the score
                weedImagePlaced = true;
                generate_random_weed_position();
            }
        } else {
            weedImagePlaced = false;
        }

        view_port_update(view_port);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);

    furi_record_close(RECORD_GUI);

    return 0;
}