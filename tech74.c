/*
 * 74 Tech - 74 Friday Tech starter app (Version 1)
 *
 * Home screen -> Menu (My Bots, My Status, Jobs, Ideas, Domes)
 * Uses only: Submenu (menu), Widget, Text Input.
 * Data saved to /ext/apps/74tech/ (the "/apps/74tech/" folder on the SD card):
 *   status.txt, jobs.txt, ideas.txt
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tech74_icons.h>

#define TAG "74Tech"

#define DATA_DIR    "/ext/apps/74tech"
#define STATUS_PATH DATA_DIR "/status.txt"
#define JOBS_PATH   DATA_DIR "/jobs.txt"
#define IDEAS_PATH  DATA_DIR "/ideas.txt"

#define BOT_COUNT    3
#define DOME_COUNT   2
#define STATUS_COUNT 9

#define JOBS_MAX        8
#define JOB_NAME_LEN    21
#define JOBS_FILE_MAX   1024
#define IDEA_MAX        64
#define IDEAS_VIEW_MAX  1536

/* ------------------------------------------------------------------ */
/* Static data                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* name;
    const char* role;
    const char* status;
} Bot;

static const Bot bots[BOT_COUNT] = {
    {"Spark", "Tech", "On Job"},
    {"Bolt", "Welcome", "Available"},
    {"Nova", "Ideas", "In Meeting"},
};

typedef struct {
    const char* name;
    uint8_t count;
    uint8_t bot_idx[BOT_COUNT];
} Dome;

static const Dome domes[DOME_COUNT] = {
    {"Main Dome", 2, {0, 1, 0}},
    {"North Dome", 1, {2, 0, 0}},
};

static const char* const status_names[STATUS_COUNT] = {
    "Good Morning",
    "In A Meeting",
    "On A Job",
    "Voice Note Received",
    "Share Idea",
    "New Member Welcome",
    "Thank You",
    "Job Done",
    "5 Star Review",
};

/* LED colours (one per status) + short vibration. The LED stays lit while
 * the status screen is shown and is switched off when you press Back. */
static const NotificationMessage msg_red_100 = {
    .type = NotificationMessageTypeLedRed,
    .data.led.value = 100,
};
static const NotificationMessage msg_green_90 = {
    .type = NotificationMessageTypeLedGreen,
    .data.led.value = 90,
};

#define LED_SEQ(seq_name, r, g, b)                                         \
    static const NotificationSequence seq_name = {                         \
        r,                                                                 \
        g,                                                                 \
        b,                                                                 \
        &message_vibro_on,                                                 \
        &message_delay_250,                                                \
        &message_vibro_off,                                                \
        &message_do_not_reset,                                             \
        NULL,                                                              \
    }

LED_SEQ(seq_good_morning, &message_red_255, &msg_green_90, &message_blue_0); /* orange */
LED_SEQ(seq_meeting, &message_red_255, &message_green_0, &message_blue_0); /* red */
LED_SEQ(seq_on_job, &message_red_0, &message_green_0, &message_blue_255); /* blue */
LED_SEQ(seq_voice, &message_red_255, &message_green_0, &message_blue_255); /* magenta */
LED_SEQ(seq_idea, &message_red_0, &message_green_255, &message_blue_255); /* cyan */
LED_SEQ(seq_welcome, &message_red_255, &message_green_255, &message_blue_255); /* white */
LED_SEQ(seq_thanks, &msg_red_100, &message_green_0, &message_blue_255); /* purple */
LED_SEQ(seq_job_done, &message_red_0, &message_green_255, &message_blue_0); /* green */
LED_SEQ(seq_five_star, &message_red_255, &message_green_255, &message_blue_0); /* yellow */

static const NotificationSequence* const status_leds[STATUS_COUNT] = {
    &seq_good_morning,
    &seq_meeting,
    &seq_on_job,
    &seq_voice,
    &seq_idea,
    &seq_welcome,
    &seq_thanks,
    &seq_job_done,
    &seq_five_star,
};

/* ------------------------------------------------------------------ */
/* App types                                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    ViewHome,
    ViewMenu,
    ViewBots,
    ViewBotProfile,
    ViewStatusList,
    ViewStatusShow,
    ViewJobs,
    ViewIdeasMenu,
    ViewIdeaInput,
    ViewIdeasList,
    ViewDomes,
    ViewDomeShow,
} ViewId;

typedef enum {
    EvMenu = 1,
    EvBot,
    EvStatus,
    EvJob,
    EvIdeas,
    EvDome,
    EvHomeOk,
    EvBotActive,
    EvIdeaDone,
} EvKind;

#define EV(kind, idx) ((((uint32_t)(kind)) << 8) | ((uint32_t)(idx)))

typedef enum {
    JobTodo,
    JobStarted,
    JobDone,
} JobState;

typedef struct {
    char name[JOB_NAME_LEN];
    uint8_t state;
} Job;

typedef struct {
    Gui* gui;
    ViewDispatcher* vd;
    NotificationApp* notif;
    Storage* storage;

    Submenu* menu;
    Submenu* bots_menu;
    Submenu* status_menu;
    Submenu* jobs_menu;
    Submenu* ideas_menu;
    Submenu* domes_menu;

    Widget* home;
    Widget* bot_profile;
    Widget* status_show;
    Widget* ideas_list;
    Widget* dome_show;

    TextInput* idea_input;

    ViewId cur;
    int8_t active_bot;
    int8_t sel_bot;
    uint8_t last_min;
    uint8_t last_bat;

    char idea_buf[IDEA_MAX];

    Job jobs[JOBS_MAX];
    uint8_t job_count;
} App;

static void goto_view(App* app, ViewId id);

/* ------------------------------------------------------------------ */
/* SD card helpers                                                     */
/* ------------------------------------------------------------------ */

static bool file_append(App* app, const char* path, const char* text) {
    storage_simply_mkdir(app->storage, DATA_DIR);
    File* f = storage_file_alloc(app->storage);
    bool ok = storage_file_open(f, path, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(ok) {
        size_t len = strlen(text);
        ok = (storage_file_write(f, text, (uint16_t)len) == len);
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

/* Reads up to cap-1 bytes (the last bytes of the file when tail = true). */
static size_t read_file(App* app, const char* path, char* buf, size_t cap, bool tail) {
    size_t n = 0;
    buf[0] = 0;
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(f);
        if(tail && size > (uint64_t)(cap - 1)) {
            storage_file_seek(f, (uint32_t)(size - (uint64_t)(cap - 1)), true);
        }
        n = storage_file_read(f, buf, (uint16_t)(cap - 1));
        buf[n] = 0;
    }
    storage_file_close(f);
    storage_file_free(f);
    return n;
}

/* ------------------------------------------------------------------ */
/* Home screen                                                         */
/* ------------------------------------------------------------------ */

static void home_btn_cb(GuiButtonType result, InputType type, void* ctx) {
    UNUSED(result);
    App* app = ctx;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->vd, EV(EvHomeOk, 0));
    }
}

static void home_build(App* app) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    uint8_t bat = furi_hal_power_get_pct();
    char line[32];

    app->last_min = dt.minute;
    app->last_bat = bat;

    widget_reset(app->home);
    widget_add_string_element(
        app->home, 64, 1, AlignCenter, AlignTop, FontPrimary, "74 FRIDAY TECH");
    widget_add_icon_element(app->home, 4, 16, &I_mascot);

    snprintf(line, sizeof(line), "Battery: %u%%", (unsigned)bat);
    widget_add_string_element(app->home, 44, 17, AlignLeft, AlignTop, FontSecondary, line);

    snprintf(line, sizeof(line), "Time: %02u:%02u", (unsigned)dt.hour, (unsigned)dt.minute);
    widget_add_string_element(app->home, 44, 29, AlignLeft, AlignTop, FontSecondary, line);

    snprintf(
        line,
        sizeof(line),
        "Bot: %s",
        (app->active_bot >= 0) ? bots[app->active_bot].name : "none");
    widget_add_string_element(app->home, 44, 41, AlignLeft, AlignTop, FontSecondary, line);

    widget_add_button_element(app->home, GuiButtonTypeCenter, "Menu", home_btn_cb, app);
}

/* ------------------------------------------------------------------ */
/* My Bots                                                             */
/* ------------------------------------------------------------------ */

static void bot_btn_cb(GuiButtonType result, InputType type, void* ctx) {
    UNUSED(result);
    App* app = ctx;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->vd, EV(EvBotActive, 0));
    }
}

static void bot_profile_build(App* app) {
    const Bot* b = &bots[app->sel_bot];
    char line[40];

    widget_reset(app->bot_profile);

    snprintf(line, sizeof(line), "Name: %s", b->name);
    widget_add_string_element(app->bot_profile, 2, 2, AlignLeft, AlignTop, FontPrimary, line);
    snprintf(line, sizeof(line), "Role: %s", b->role);
    widget_add_string_element(app->bot_profile, 2, 15, AlignLeft, AlignTop, FontPrimary, line);
    snprintf(line, sizeof(line), "Status: %s", b->status);
    widget_add_string_element(app->bot_profile, 2, 28, AlignLeft, AlignTop, FontPrimary, line);

    if(app->active_bot == app->sel_bot) {
        widget_add_string_element(
            app->bot_profile, 64, 41, AlignCenter, AlignTop, FontSecondary, "* ACTIVE BOT *");
    } else {
        widget_add_button_element(
            app->bot_profile, GuiButtonTypeCenter, "Set Active", bot_btn_cb, app);
    }
}

/* ------------------------------------------------------------------ */
/* My Status                                                           */
/* ------------------------------------------------------------------ */

static void status_show(App* app, uint8_t idx) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);

    char line[80];
    snprintf(
        line,
        sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u | %s\n",
        (unsigned)dt.year,
        (unsigned)dt.month,
        (unsigned)dt.day,
        (unsigned)dt.hour,
        (unsigned)dt.minute,
        (unsigned)dt.second,
        status_names[idx]);
    bool saved = file_append(app, STATUS_PATH, line);

    FuriString* s = furi_string_alloc();
    furi_string_printf(s, "\e#%s\e#", status_names[idx]);

    widget_reset(app->status_show);
    widget_add_icon_element(app->status_show, 4, 10, &I_mascot);
    widget_add_text_box_element(
        app->status_show, 42, 6, 84, 44, AlignLeft, AlignCenter, furi_string_get_cstr(s), false);
    if(!saved) {
        widget_add_string_element(
            app->status_show, 64, 54, AlignCenter, AlignTop, FontSecondary, "SD save failed!");
    }
    furi_string_free(s);

    notification_message(app->notif, status_leds[idx]);
}

/* ------------------------------------------------------------------ */
/* Jobs                                                                */
/* ------------------------------------------------------------------ */

static void submenu_cb(void* ctx, uint32_t index) {
    App* app = ctx;
    view_dispatcher_send_custom_event(app->vd, index);
}

static bool jobs_save(App* app) {
    storage_simply_mkdir(app->storage, DATA_DIR);
    File* f = storage_file_alloc(app->storage);
    bool ok = storage_file_open(f, JOBS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        char line[48];
        for(uint8_t i = 0; i < app->job_count; i++) {
            const char* st = "TODO";
            if(app->jobs[i].state == JobStarted) st = "STARTED";
            if(app->jobs[i].state == JobDone) st = "DONE";
            int len = snprintf(line, sizeof(line), "%s|%s\n", app->jobs[i].name, st);
            if(len < 0 || storage_file_write(f, line, (uint16_t)len) != (size_t)len) {
                ok = false;
            }
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

static void jobs_load(App* app) {
    app->job_count = 0;

    if(!storage_file_exists(app->storage, JOBS_PATH)) {
        /* First run: create jobs.txt with 3 example jobs */
        static const char* const defaults[3] = {"Fix Router", "Setup CCTV", "Smart Lock"};
        for(uint8_t i = 0; i < 3; i++) {
            snprintf(app->jobs[i].name, JOB_NAME_LEN, "%s", defaults[i]);
            app->jobs[i].state = JobTodo;
        }
        app->job_count = 3;
        if(!jobs_save(app)) {
            FURI_LOG_E(TAG, "Could not create jobs.txt");
        }
        return;
    }

    char* buf = malloc(JOBS_FILE_MAX);
    read_file(app, JOBS_PATH, buf, JOBS_FILE_MAX, false);

    char* p = buf;
    while(*p && app->job_count < JOBS_MAX) {
        char* eol = strchr(p, '\n');
        if(eol) *eol = 0;
        size_t l = strlen(p);
        if(l > 0 && p[l - 1] == '\r') p[l - 1] = 0;

        if(p[0]) {
            Job* j = &app->jobs[app->job_count];
            uint8_t state = JobTodo;
            char* bar = strrchr(p, '|');
            if(bar) {
                *bar = 0;
                if(strcmp(bar + 1, "STARTED") == 0) state = JobStarted;
                if(strcmp(bar + 1, "DONE") == 0) state = JobDone;
            }
            size_t n = strlen(p);
            if(n > JOB_NAME_LEN - 1) n = JOB_NAME_LEN - 1;
            memcpy(j->name, p, n);
            j->name[n] = 0;
            j->state = state;
            app->job_count++;
        }

        if(!eol) break;
        p = eol + 1;
    }
    free(buf);
}

static void jobs_refresh_menu(App* app, uint8_t selected) {
    submenu_reset(app->jobs_menu);
    submenu_set_header(app->jobs_menu, "Jobs: OK=Start/Finish");
    for(uint8_t i = 0; i < app->job_count; i++) {
        const char* mark = "[ ]";
        if(app->jobs[i].state == JobStarted) mark = "[>]";
        if(app->jobs[i].state == JobDone) mark = "[x]";
        char label[JOB_NAME_LEN + 6];
        snprintf(label, sizeof(label), "%s %s", mark, app->jobs[i].name);
        submenu_add_item(app->jobs_menu, label, EV(EvJob, i), submenu_cb, app);
    }
    if(selected < app->job_count) {
        submenu_set_selected_item(app->jobs_menu, EV(EvJob, selected));
    }
}

/* ------------------------------------------------------------------ */
/* Ideas                                                               */
/* ------------------------------------------------------------------ */

static void idea_input_cb(void* ctx) {
    App* app = ctx;
    view_dispatcher_send_custom_event(app->vd, EV(EvIdeaDone, 0));
}

static void ideas_list_build(App* app) {
    char* buf = malloc(IDEAS_VIEW_MAX);
    size_t n = read_file(app, IDEAS_PATH, buf, IDEAS_VIEW_MAX, true);

    FuriString* s = furi_string_alloc();
    furi_string_cat_str(s, "\e#Saved Ideas\e#\n");

    char* p = buf;
    if(n >= IDEAS_VIEW_MAX - 1) {
        /* Buffer starts mid-file: drop the partial first line */
        char* nl = strchr(p, '\n');
        if(nl) p = nl + 1;
    }

    bool any = false;
    while(*p) {
        char* eol = strchr(p, '\n');
        if(eol) *eol = 0;
        size_t l = strlen(p);
        if(l > 0 && p[l - 1] == '\r') p[l - 1] = 0;
        if(p[0]) {
            furi_string_cat_printf(s, "- %s\n", p);
            any = true;
        }
        if(!eol) break;
        p = eol + 1;
    }
    if(!any) furi_string_cat_str(s, "No ideas saved yet.");

    widget_reset(app->ideas_list);
    widget_add_text_scroll_element(app->ideas_list, 0, 0, 128, 64, furi_string_get_cstr(s));

    furi_string_free(s);
    free(buf);
}

/* ------------------------------------------------------------------ */
/* Domes                                                               */
/* ------------------------------------------------------------------ */

static void dome_show_build(App* app, uint8_t idx) {
    const Dome* d = &domes[idx];
    FuriString* s = furi_string_alloc();
    furi_string_printf(s, "\e#%s\e#\nBots:\n", d->name);
    for(uint8_t i = 0; i < d->count; i++) {
        furi_string_cat_printf(s, "- %s\n", bots[d->bot_idx[i]].name);
    }
    widget_reset(app->dome_show);
    widget_add_text_box_element(
        app->dome_show, 2, 2, 124, 60, AlignLeft, AlignTop, furi_string_get_cstr(s), false);
    furi_string_free(s);
}

/* ------------------------------------------------------------------ */
/* Navigation + events                                                 */
/* ------------------------------------------------------------------ */

static void goto_view(App* app, ViewId id) {
    if(id == ViewHome) home_build(app);
    app->cur = id;
    view_dispatcher_switch_to_view(app->vd, id);
}

static bool nav_cb(void* ctx) {
    App* app = ctx;
    switch(app->cur) {
    case ViewHome:
        return false; /* Back on home exits the app */
    case ViewMenu:
        goto_view(app, ViewHome);
        return true;
    case ViewBots:
    case ViewStatusList:
    case ViewJobs:
    case ViewIdeasMenu:
    case ViewDomes:
        goto_view(app, ViewMenu);
        return true;
    case ViewBotProfile:
        goto_view(app, ViewBots);
        return true;
    case ViewStatusShow:
        notification_message(app->notif, &sequence_reset_rgb);
        goto_view(app, ViewStatusList);
        return true;
    case ViewIdeaInput:
    case ViewIdeasList:
        goto_view(app, ViewIdeasMenu);
        return true;
    case ViewDomeShow:
        goto_view(app, ViewDomes);
        return true;
    }
    return false;
}

static bool custom_cb(void* ctx, uint32_t event) {
    App* app = ctx;
    uint32_t kind = event >> 8;
    uint8_t idx = (uint8_t)(event & 0xFF);

    switch((EvKind)kind) {
    case EvHomeOk:
        goto_view(app, ViewMenu);
        return true;

    case EvMenu:
        switch(idx) {
        case 0:
            goto_view(app, ViewBots);
            break;
        case 1:
            goto_view(app, ViewStatusList);
            break;
        case 2:
            jobs_load(app);
            jobs_refresh_menu(app, 0);
            goto_view(app, ViewJobs);
            break;
        case 3:
            goto_view(app, ViewIdeasMenu);
            break;
        case 4:
            goto_view(app, ViewDomes);
            break;
        default:
            break;
        }
        return true;

    case EvBot:
        if(idx < BOT_COUNT) {
            app->sel_bot = (int8_t)idx;
            bot_profile_build(app);
            goto_view(app, ViewBotProfile);
        }
        return true;

    case EvBotActive:
        app->active_bot = app->sel_bot;
        bot_profile_build(app);
        return true;

    case EvStatus:
        if(idx < STATUS_COUNT) {
            status_show(app, idx);
            goto_view(app, ViewStatusShow);
        }
        return true;

    case EvJob:
        if(idx < app->job_count) {
            if(app->jobs[idx].state == JobTodo) {
                app->jobs[idx].state = JobStarted;
            } else if(app->jobs[idx].state == JobStarted) {
                app->jobs[idx].state = JobDone;
            }
            if(!jobs_save(app)) {
                FURI_LOG_E(TAG, "Could not save jobs.txt");
            }
            jobs_refresh_menu(app, idx);
        }
        return true;

    case EvIdeas:
        if(idx == 0) {
            app->idea_buf[0] = 0;
            text_input_reset(app->idea_input);
            text_input_set_header_text(app->idea_input, "Type your idea");
            text_input_set_result_callback(
                app->idea_input, idea_input_cb, app, app->idea_buf, IDEA_MAX, true);
            goto_view(app, ViewIdeaInput);
        } else {
            ideas_list_build(app);
            goto_view(app, ViewIdeasList);
        }
        return true;

    case EvIdeaDone:
        if(app->idea_buf[0]) {
            char line[IDEA_MAX + 2];
            snprintf(line, sizeof(line), "%s\n", app->idea_buf);
            if(!file_append(app, IDEAS_PATH, line)) {
                FURI_LOG_E(TAG, "Could not save ideas.txt");
            }
        }
        ideas_list_build(app);
        goto_view(app, ViewIdeasList);
        return true;

    case EvDome:
        if(idx < DOME_COUNT) {
            dome_show_build(app, idx);
            goto_view(app, ViewDomeShow);
        }
        return true;
    }
    return false;
}

static void tick_cb(void* ctx) {
    App* app = ctx;
    if(app->cur != ViewHome) return;
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    uint8_t bat = furi_hal_power_get_pct();
    if(dt.minute != app->last_min || bat != app->last_bat) {
        home_build(app);
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int32_t tech74_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->active_bot = -1;
    app->sel_bot = 0;

    app->gui = furi_record_open(RECORD_GUI);
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(app->storage, DATA_DIR);

    app->vd = view_dispatcher_alloc();

    app->menu = submenu_alloc();
    app->bots_menu = submenu_alloc();
    app->status_menu = submenu_alloc();
    app->jobs_menu = submenu_alloc();
    app->ideas_menu = submenu_alloc();
    app->domes_menu = submenu_alloc();
    app->home = widget_alloc();
    app->bot_profile = widget_alloc();
    app->status_show = widget_alloc();
    app->ideas_list = widget_alloc();
    app->dome_show = widget_alloc();
    app->idea_input = text_input_alloc();

    /* Main menu: only 5 items */
    submenu_set_header(app->menu, "74 Tech");
    submenu_add_item(app->menu, "My Bots", EV(EvMenu, 0), submenu_cb, app);
    submenu_add_item(app->menu, "My Status", EV(EvMenu, 1), submenu_cb, app);
    submenu_add_item(app->menu, "Jobs", EV(EvMenu, 2), submenu_cb, app);
    submenu_add_item(app->menu, "Ideas", EV(EvMenu, 3), submenu_cb, app);
    submenu_add_item(app->menu, "Domes", EV(EvMenu, 4), submenu_cb, app);

    /* My Bots list */
    submenu_set_header(app->bots_menu, "My Bots");
    for(uint8_t i = 0; i < BOT_COUNT; i++) {
        submenu_add_item(app->bots_menu, bots[i].name, EV(EvBot, i), submenu_cb, app);
    }

    /* My Status list */
    submenu_set_header(app->status_menu, "My Status");
    for(uint8_t i = 0; i < STATUS_COUNT; i++) {
        submenu_add_item(app->status_menu, status_names[i], EV(EvStatus, i), submenu_cb, app);
    }

    /* Ideas menu */
    submenu_set_header(app->ideas_menu, "Ideas");
    submenu_add_item(app->ideas_menu, "Add Idea", EV(EvIdeas, 0), submenu_cb, app);
    submenu_add_item(app->ideas_menu, "List Ideas", EV(EvIdeas, 1), submenu_cb, app);

    /* Domes list */
    submenu_set_header(app->domes_menu, "Domes");
    for(uint8_t i = 0; i < DOME_COUNT; i++) {
        submenu_add_item(app->domes_menu, domes[i].name, EV(EvDome, i), submenu_cb, app);
    }

    view_dispatcher_add_view(app->vd, ViewHome, widget_get_view(app->home));
    view_dispatcher_add_view(app->vd, ViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->vd, ViewBots, submenu_get_view(app->bots_menu));
    view_dispatcher_add_view(app->vd, ViewBotProfile, widget_get_view(app->bot_profile));
    view_dispatcher_add_view(app->vd, ViewStatusList, submenu_get_view(app->status_menu));
    view_dispatcher_add_view(app->vd, ViewStatusShow, widget_get_view(app->status_show));
    view_dispatcher_add_view(app->vd, ViewJobs, submenu_get_view(app->jobs_menu));
    view_dispatcher_add_view(app->vd, ViewIdeasMenu, submenu_get_view(app->ideas_menu));
    view_dispatcher_add_view(app->vd, ViewIdeaInput, text_input_get_view(app->idea_input));
    view_dispatcher_add_view(app->vd, ViewIdeasList, widget_get_view(app->ideas_list));
    view_dispatcher_add_view(app->vd, ViewDomes, submenu_get_view(app->domes_menu));
    view_dispatcher_add_view(app->vd, ViewDomeShow, widget_get_view(app->dome_show));

    view_dispatcher_set_event_callback_context(app->vd, app);
    view_dispatcher_set_custom_event_callback(app->vd, custom_cb);
    view_dispatcher_set_navigation_event_callback(app->vd, nav_cb);
    view_dispatcher_set_tick_event_callback(app->vd, tick_cb, 1000);

    view_dispatcher_attach_to_gui(app->vd, app->gui, ViewDispatcherTypeFullscreen);

    goto_view(app, ViewHome);
    view_dispatcher_run(app->vd);

    /* Cleanup */
    notification_message(app->notif, &sequence_reset_rgb);

    view_dispatcher_remove_view(app->vd, ViewHome);
    view_dispatcher_remove_view(app->vd, ViewMenu);
    view_dispatcher_remove_view(app->vd, ViewBots);
    view_dispatcher_remove_view(app->vd, ViewBotProfile);
    view_dispatcher_remove_view(app->vd, ViewStatusList);
    view_dispatcher_remove_view(app->vd, ViewStatusShow);
    view_dispatcher_remove_view(app->vd, ViewJobs);
    view_dispatcher_remove_view(app->vd, ViewIdeasMenu);
    view_dispatcher_remove_view(app->vd, ViewIdeaInput);
    view_dispatcher_remove_view(app->vd, ViewIdeasList);
    view_dispatcher_remove_view(app->vd, ViewDomes);
    view_dispatcher_remove_view(app->vd, ViewDomeShow);

    submenu_free(app->menu);
    submenu_free(app->bots_menu);
    submenu_free(app->status_menu);
    submenu_free(app->jobs_menu);
    submenu_free(app->ideas_menu);
    submenu_free(app->domes_menu);
    widget_free(app->home);
    widget_free(app->bot_profile);
    widget_free(app->status_show);
    widget_free(app->ideas_list);
    widget_free(app->dome_show);
    text_input_free(app->idea_input);
    view_dispatcher_free(app->vd);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
