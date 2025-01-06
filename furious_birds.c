#include <gui/icon_i.h>
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <furi_hal.h>
#include <stdlib.h>

/* generated by fbt from .png files in images folder */
#include <furious_birds_icons.h>

const uint8_t RED_START_X = 9;
const uint8_t RED_CENTER_X = 7;
const uint8_t RED_CENTER_Y = 8;

const uint8_t SLINGSHOT_X = 24;
const uint8_t SLINGSHOT_CENTER_X = 4;
const uint8_t SLINGSHOT_Y = 35;

const uint8_t RED_TO_SLINGSHOT_X = SLINGSHOT_X - RED_START_X;

const int8_t ANGLE_START = 15;
const int8_t ANGLE_MAX = 45;
const int8_t ANGLE_MIN = -45;

const uint8_t PIG_CENTER_X = 6;
const uint8_t PIG_CENTER_Y = 6;

const uint8_t PIGS_AREA_X_START = 50;
const uint8_t PIGS_AREA_X_END = 120;
const uint8_t PIGS_AREA_Y_START = 8;
const uint8_t PIGS_AREA_Y_END = 56;

const uint8_t PIG_COUNT = 10;
const uint8_t MIN_DISTANCE_BETWEEN_PIGS = 14;

const uint8_t AIMING_LINE_LENGTH = 20;

enum GameStatus {
    GameStatusAiming,
    GameStatusFlying,
};

typedef struct {
    uint8_t x;
    uint8_t y;
    bool visible;
} Pig;

typedef struct {
    uint8_t x;
    uint8_t y;
} Red;

typedef struct {
    FuriMutex* mutex;
    Red* red;
    Pig* pigs[10]; // must have same value as PIG_COUNT
    int8_t angle;
    double_t angleRadians;
    uint8_t status;
    double_t diff;
    int8_t diffInt;
} AppState;

void draw_red(Canvas* canvas, AppState* state) {
    canvas_draw_icon(canvas, state->red->x - RED_CENTER_X, state->red->y - RED_CENTER_Y, &I_Red);
    // canvas_draw_dot(canvas, state->red->x, state->red->y);
}

void draw_slingshot(Canvas* canvas) {
    canvas_draw_icon(canvas, SLINGSHOT_X - SLINGSHOT_CENTER_X, SLINGSHOT_Y, &I_Slingshot);
    // canvas_draw_dot(canvas, SLINGSHOT_X, SLINGSHOT_Y);
}

void draw_debug_info(Canvas* canvas, AppState* appState) {
    FuriString* xstr = furi_string_alloc();
    furi_string_printf(xstr, "angle: %d", appState->angle);
    canvas_draw_str(canvas, 0, 8, furi_string_get_cstr(xstr));
    furi_string_printf(xstr, "red y: %d", appState->red->y);
    canvas_draw_str(canvas, 0, 16, furi_string_get_cstr(xstr));
    furi_string_free(xstr);
}

void draw_pigs(Canvas* canvas, AppState* appState) {
    for(uint8_t i = 0; i < PIG_COUNT; i++) {
        Pig* pig = appState->pigs[i];
        if(pig->visible) {
            canvas_draw_icon(canvas, pig->x - PIG_CENTER_X, pig->y - PIG_CENTER_Y, &I_Pig);
            // canvas_draw_dot(canvas, pig->x, pig->y);
        }
    }
}

void draw_aiming_line(Canvas* canvas, AppState* appState) {
    uint8_t x = AIMING_LINE_LENGTH * cos(appState->angleRadians);
    int8_t y = AIMING_LINE_LENGTH * sin(appState->angleRadians);
    canvas_draw_line(canvas, SLINGSHOT_X, SLINGSHOT_Y, SLINGSHOT_X + x, SLINGSHOT_Y - y);
}

uint8_t calculate_red_start_y(AppState* appState) {
    appState->diff = tan(appState->angleRadians);
    appState->diffInt = RED_TO_SLINGSHOT_X * appState->diff;
    return SLINGSHOT_Y + appState->diffInt;
}

static void game_draw_callback(Canvas* canvas, void* ctx) {
    AppState* appState = (AppState*)ctx;
    furi_mutex_acquire(appState->mutex, FuriWaitForever);

    draw_red(canvas, appState);
    draw_slingshot(canvas);
    draw_aiming_line(canvas, appState);
    draw_pigs(canvas, appState);

    draw_debug_info(canvas, appState);

    furi_mutex_release(appState->mutex);
}

static void game_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;

    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

uint8_t distance_between(Pig* pig1, Pig* pig2) {
    return sqrt(
        (pig2->x - pig1->x) * (pig2->x - pig1->x) + (pig2->y - pig1->y) * (pig2->y - pig1->y));
}

Pig* create_random_pig(uint8_t i, Pig* pigs[]) {
    Pig* pig = malloc(sizeof(Pig));
    pig->visible = true;

    bool intercept = false;
    do {
        pig->x =
            (furi_hal_random_get() % (PIGS_AREA_X_END - PIGS_AREA_X_START)) + PIGS_AREA_X_START;
        pig->y =
            (furi_hal_random_get() % (PIGS_AREA_Y_END - PIGS_AREA_Y_START)) + PIGS_AREA_Y_START;

        intercept = false;
        for(uint8_t j = 0; j < i; j++) {
            if(distance_between(pigs[j], pig) < MIN_DISTANCE_BETWEEN_PIGS) {
                intercept = true;
                break;
            }
        }
    } while(intercept);

    return pig;
}

int32_t furious_birds_app(void* p) {
    UNUSED(p);

    InputEvent event;
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    AppState* appState = malloc(sizeof(AppState));
    appState->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    appState->angle = ANGLE_START;
    appState->angleRadians = ((double_t)appState->angle) / 180 * ((double_t)M_PI);
    appState->status = GameStatusAiming;
    for(uint8_t i = 0; i < PIG_COUNT; i++) {
        Pig* pig = create_random_pig(i, appState->pigs);
        appState->pigs[i] = pig;
    }

    Red* red = malloc(sizeof(Red));
    red->x = RED_START_X;
    red->y = calculate_red_start_y(appState);
    appState->red = red;

    // Configure view port
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, game_draw_callback, appState);
    view_port_input_callback_set(view_port, game_input_callback, event_queue);

    // Register view port in GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    bool isFinishing = false;
    while(!isFinishing) {
        furi_check(furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk);

        if((event.type != InputTypeShort) && (event.type != InputTypeLong) &&
           (event.type != InputTypeRepeat))
            continue;

        furi_mutex_acquire(appState->mutex, FuriWaitForever);

        if(appState->status == GameStatusAiming) {
            if(event.key == InputKeyUp && appState->angle > -45) {
                appState->angle--;
                appState->angleRadians = ((double_t)appState->angle) / 180 * ((double_t)M_PI);
                red->y = calculate_red_start_y(appState);
            } else if(event.key == InputKeyDown && appState->angle < 45) {
                appState->angle++;
                appState->angleRadians = ((double_t)appState->angle) / 180 * ((double_t)M_PI);
                red->y = calculate_red_start_y(appState);
            } else if(event.key == InputKeyBack) {
                isFinishing = true;
            }
        }
        view_port_update(view_port);
        furi_mutex_release(appState->mutex);
    }

    furi_message_queue_free(event_queue);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);

    // Should happen after freeing viewport because draw callback could try to acquire mutex.
    furi_mutex_free(appState->mutex);
    free(red);
    for(uint8_t i = 0; i < PIG_COUNT; i++) {
        free(appState->pigs[i]);
    }
    free(appState);

    return 0;
}
