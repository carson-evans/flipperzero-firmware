#include "fatfs.h"
#include "filesystem-api.h"
#include "sd-filesystem.h"
#include "menu/menu.h"
#include "menu/menu_item.h"
#include "cli/cli.h"
#include "api-hal-sd.h"

FS_Api* fs_api_alloc() {
    FS_Api* fs_api = furi_alloc(sizeof(FS_Api));

    // fill file api
    fs_api->file.open = fs_file_open;
    fs_api->file.close = fs_file_close;
    fs_api->file.read = fs_file_read;
    fs_api->file.write = fs_file_write;
    fs_api->file.seek = fs_file_seek;
    fs_api->file.tell = fs_file_tell;
    fs_api->file.truncate = fs_file_truncate;
    fs_api->file.size = fs_file_size;
    fs_api->file.sync = fs_file_sync;
    fs_api->file.eof = fs_file_eof;

    // fill dir api
    fs_api->dir.open = fs_dir_open;
    fs_api->dir.close = fs_dir_close;
    fs_api->dir.read = fs_dir_read;
    fs_api->dir.rewind = fs_dir_rewind;

    // fill common api
    fs_api->common.info = fs_common_info;
    fs_api->common.remove = fs_common_remove;
    fs_api->common.rename = fs_common_rename;
    fs_api->common.set_attr = fs_common_set_attr;
    fs_api->common.mkdir = fs_common_mkdir;
    fs_api->common.set_time = fs_common_set_time;
    fs_api->common.get_fs_info = fs_get_fs_info;

    // fill errors api
    fs_api->error.get_desc = fs_error_get_desc;
    fs_api->error.get_internal_desc = fs_error_get_internal_desc;

    return fs_api;
}

void sd_set_lines(SdApp* sd_app, uint8_t count, ...) {
    va_list argptr;
    count = min(count, SD_STATE_LINES_COUNT);

    for(uint8_t i = 0; i < SD_STATE_LINES_COUNT; i++) {
        sd_app->line[i] = "";
    }

    va_start(argptr, count);

    for(uint8_t i = 0; i < count; i++) {
        sd_app->line[i] = va_arg(argptr, char*);
    }

    va_end(argptr);
}

void sd_icon_draw_callback(Canvas* canvas, void* context) {
    furi_assert(canvas);
    furi_assert(context);
    SdApp* sd_app = context;

    switch(sd_app->info.status) {
    case SD_NO_CARD:
        break;
    case SD_OK:
        canvas_draw_icon(canvas, 0, 0, sd_app->icon.mounted);
        break;
    default:
        canvas_draw_icon(canvas, 0, 0, sd_app->icon.fail);
        break;
    }
}

void sd_app_draw_callback(Canvas* canvas, void* context) {
    furi_assert(canvas);
    furi_assert(context);
    SdApp* sd_app = context;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);

    for(uint8_t i = 0; i < SD_STATE_LINES_COUNT; i++) {
        canvas_draw_str(canvas, 0, (i + 1) * 10, sd_app->line[i]);
    }
}

void sd_app_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    SdApp* sd_app = context;

    osMessageQueuePut(sd_app->event_queue, event, 0, 0);
}

SdApp* sd_app_alloc() {
    SdApp* sd_app = furi_alloc(sizeof(SdApp));

    // init inner fs data
    furi_check(_fs_init(&sd_app->info));

    sd_app->event_queue = osMessageQueueNew(8, sizeof(InputEvent), NULL);

    // init view_port
    sd_app->view_port = view_port_alloc();
    view_port_draw_callback_set(sd_app->view_port, sd_app_draw_callback, sd_app);
    view_port_input_callback_set(sd_app->view_port, sd_app_input_callback, sd_app);
    view_port_enabled_set(sd_app->view_port, false);

    // init lines
    sd_set_lines(sd_app, 0);

    // init icon view_port
    sd_app->icon.view_port = view_port_alloc();
    sd_app->icon.mounted = assets_icons_get(I_SDcardMounted_11x8);
    sd_app->icon.fail = assets_icons_get(I_SDcardFail_11x8);
    view_port_set_width(sd_app->icon.view_port, icon_get_width(sd_app->icon.mounted));
    view_port_draw_callback_set(sd_app->icon.view_port, sd_icon_draw_callback, sd_app);
    view_port_enabled_set(sd_app->icon.view_port, false);

    return sd_app;
}

bool app_sd_ask(SdApp* sd_app, InputKey input_true, InputKey input_false) {
    bool result;

    InputEvent event;
    while(1) {
        osStatus_t event_status =
            osMessageQueueGet(sd_app->event_queue, &event, NULL, osWaitForever);

        if(event_status == osOK) {
            if(event.type == InputTypeShort && event.key == input_true) {
                result = true;
                break;
            }
            if(event.type == InputTypeShort && event.key == InputKeyBack) {
                result = false;
                break;
            }
        }
    }

    return result;
}

void app_sd_info_callback(void* context) {
    furi_assert(context);
    SdApp* sd_app = context;
    view_port_enabled_set(sd_app->view_port, true);

    // dynamic strings
    const uint8_t str_buffer_size = 26;
    const uint8_t str_count = 6;
    char* str_buffer[str_count];
    bool memory_error = false;

    // info vars
    uint32_t serial_num;
    SDError get_label_result, get_free_result;
    FATFS* fs;
    uint32_t free_clusters, free_sectors, total_sectors;
    char volume_label[34];

    // init strings
    for(uint8_t i = 0; i < str_count; i++) {
        str_buffer[i] = malloc(str_buffer_size + 1);
        if(str_buffer[i] == NULL) {
            memory_error = true;
        } else {
            str_buffer[i][0] = 0;
        }
    }

    if(memory_error) {
        sd_set_lines(sd_app, 1, "not enough memory");
    } else {
        // get fs info
        _fs_lock(&sd_app->info);
        get_label_result = f_getlabel(sd_app->info.path, volume_label, &serial_num);
        get_free_result = f_getfree(sd_app->info.path, &free_clusters, &fs);
        _fs_unlock(&sd_app->info);

        // calculate size
        total_sectors = (fs->n_fatent - 2) * fs->csize;
        free_sectors = free_clusters * fs->csize;
        uint16_t sector_size = _MAX_SS;
#if _MAX_SS != _MIN_SS
        sector_size = fs->ssize;
#endif

        // output info to dynamic strings
        if(get_label_result == SD_OK && get_free_result == SD_OK) {
            snprintf(str_buffer[0], str_buffer_size, "%s", volume_label);

            const char* fs_type = "";

            switch(fs->fs_type) {
            case(FS_FAT12):
                fs_type = "FAT12";
                break;
            case(FS_FAT16):
                fs_type = "FAT16";
                break;
            case(FS_FAT32):
                fs_type = "FAT32";
                break;
            case(FS_EXFAT):
                fs_type = "EXFAT";
                break;
            default:
                fs_type = "UNKNOWN";
                break;
            }

            snprintf(str_buffer[1], str_buffer_size, "%s, S/N: %lu", fs_type, serial_num);

            snprintf(str_buffer[2], str_buffer_size, "Cluster: %d sectors", fs->csize);
            snprintf(str_buffer[3], str_buffer_size, "Sector: %d bytes", sector_size);
            snprintf(
                str_buffer[4], str_buffer_size, "%lu KB total", total_sectors / 1024 * sector_size);
            snprintf(
                str_buffer[5], str_buffer_size, "%lu KB free", free_sectors / 1024 * sector_size);
        } else {
            snprintf(str_buffer[0], str_buffer_size, "SD status error:");
            snprintf(
                str_buffer[1],
                str_buffer_size,
                "%s",
                fs_error_get_internal_desc(_fs_status(&sd_app->info)));
            snprintf(str_buffer[2], str_buffer_size, "Label error:");
            snprintf(
                str_buffer[3], str_buffer_size, "%s", fs_error_get_internal_desc(get_label_result));
            snprintf(str_buffer[4], str_buffer_size, "Get free error:");
            snprintf(
                str_buffer[5], str_buffer_size, "%s", fs_error_get_internal_desc(get_free_result));
        }

        // dynamic strings to screen
        sd_set_lines(
            sd_app,
            6,
            str_buffer[0],
            str_buffer[1],
            str_buffer[2],
            str_buffer[3],
            str_buffer[4],
            str_buffer[5]);
    }

    app_sd_ask(sd_app, InputKeyBack, InputKeyBack);

    sd_set_lines(sd_app, 0);
    view_port_enabled_set(sd_app->view_port, false);

    for(uint8_t i = 0; i < str_count; i++) {
        free(str_buffer[i]);
    }
}

void app_sd_format_internal(SdApp* sd_app) {
    uint8_t* work_area;

    _fs_lock(&sd_app->info);
    work_area = malloc(_MAX_SS);
    if(work_area == NULL) {
        sd_app->info.status = SD_NOT_ENOUGH_CORE;
    } else {
        sd_app->info.status = f_mkfs(sd_app->info.path, FM_ANY, 0, work_area, _MAX_SS);
        free(work_area);

        if(sd_app->info.status == SD_OK) {
            // set label and mount card
            f_setlabel("Flipper SD");
            sd_app->info.status = f_mount(&sd_app->info.fat_fs, sd_app->info.path, 1);
        }
    }

    _fs_unlock(&sd_app->info);
}

void app_sd_format_callback(void* context) {
    furi_assert(context);
    SdApp* sd_app = context;

    // ask to really format
    sd_set_lines(sd_app, 2, "Press UP to format", "or BACK to exit");
    view_port_enabled_set(sd_app->view_port, true);

    // wait for input
    if(!app_sd_ask(sd_app, InputKeyUp, InputKeyBack)) {
        view_port_enabled_set(sd_app->view_port, false);
        return;
    }

    // show warning
    sd_set_lines(sd_app, 3, "formatting SD card", "procedure can be lengthy", "please wait");

    // format card
    app_sd_format_internal(sd_app);

    if(sd_app->info.status != SD_OK) {
        sd_set_lines(
            sd_app, 2, "SD card format error", fs_error_get_internal_desc(sd_app->info.status));
    } else {
        sd_set_lines(sd_app, 1, "SD card formatted");
    }

    // wait for BACK
    app_sd_ask(sd_app, InputKeyBack, InputKeyBack);

    view_port_enabled_set(sd_app->view_port, false);
}

void app_sd_notify_wait_on() {
    api_hal_light_set(LightRed, 0xFF);
    api_hal_light_set(LightBlue, 0xFF);
}

void app_sd_notify_wait_off() {
    api_hal_light_set(LightRed, 0x00);
    api_hal_light_set(LightBlue, 0x00);
}

void app_sd_notify_success() {
    for(uint8_t i = 0; i < 3; i++) {
        delay(50);
        api_hal_light_set(LightGreen, 0xFF);
        delay(50);
        api_hal_light_set(LightGreen, 0x00);
    }
}

void app_sd_notify_eject() {
    for(uint8_t i = 0; i < 3; i++) {
        delay(50);
        api_hal_light_set(LightBlue, 0xFF);
        delay(50);
        api_hal_light_set(LightBlue, 0x00);
    }
}

void app_sd_notify_error() {
    for(uint8_t i = 0; i < 3; i++) {
        delay(50);
        api_hal_light_set(LightRed, 0xFF);
        delay(50);
        api_hal_light_set(LightRed, 0x00);
    }
}

bool app_sd_mount_card(SdApp* sd_app) {
    bool result = false;
    const uint8_t max_init_counts = 10;
    uint8_t counter = max_init_counts;
    uint8_t bsp_result;

    _fs_lock(&sd_app->info);

    while(result == false && counter > 0 && hal_sd_detect()) {
        app_sd_notify_wait_on();

        if((counter % 10) == 0) {
            // power reset sd card
            bsp_result = BSP_SD_Init(true);
        } else {
            bsp_result = BSP_SD_Init(false);
        }

        if(bsp_result) {
            // bsp error
            sd_app->info.status = SD_LOW_LEVEL_ERR;
        } else {
            sd_app->info.status = f_mount(&sd_app->info.fat_fs, sd_app->info.path, 1);

            if(sd_app->info.status == SD_OK || sd_app->info.status == SD_NO_FILESYSTEM) {
                FATFS* fs;
                uint32_t free_clusters;

                sd_app->info.status = f_getfree(sd_app->info.path, &free_clusters, &fs);

                if(sd_app->info.status == SD_OK || sd_app->info.status == SD_NO_FILESYSTEM) {
                    result = true;
                }
            }
        }
        app_sd_notify_wait_off();

        if(!result) {
            delay(1000);
            printf(
                "[sd_filesystem] init(%d), error: %s\r\n",
                counter,
                fs_error_get_internal_desc(sd_app->info.status));

            counter--;
        }
    }

    _fs_unlock(&sd_app->info);
    return result;
}

void app_sd_unmount_card(SdApp* sd_app) {
    _fs_lock(&sd_app->info);

    // set status
    sd_app->info.status = SD_NO_CARD;
    view_port_enabled_set(sd_app->icon.view_port, false);

    // close files
    for(uint8_t index = 0; index < SD_FS_MAX_FILES; index++) {
        FileData* filedata = &sd_app->info.files[index];

        if(filedata->thread_id != NULL) {
            if(filedata->is_dir) {
                f_closedir(&filedata->data.dir);
            } else {
                f_close(&filedata->data.file);
            }
            filedata->thread_id = NULL;
        }
    }

    // unmount volume
    f_mount(0, sd_app->info.path, 0);

    _fs_unlock(&sd_app->info);
}

void app_sd_eject_callback(void* context) {
    furi_assert(context);
    SdApp* sd_app = context;

    sd_set_lines(sd_app, 1, "ejecting SD card");
    view_port_enabled_set(sd_app->view_port, true);

    app_sd_unmount_card(sd_app);

    sd_set_lines(sd_app, 1, "SD card can be pulled out");

    // wait for BACK
    app_sd_ask(sd_app, InputKeyBack, InputKeyBack);

    view_port_enabled_set(sd_app->view_port, false);
}

static void cli_sd_status(string_t args, void* _ctx) {
    SdApp* sd_app = (SdApp*)_ctx;

    printf("SD status: ");
    printf(fs_error_get_internal_desc(sd_app->info.status));
    printf("\r\n");
}

static void cli_sd_format(string_t args, void* _ctx) {
    SdApp* sd_app = (SdApp*)_ctx;

    printf("formatting SD card, please wait\r\n");

    // format card
    app_sd_format_internal(sd_app);

    if(sd_app->info.status != SD_OK) {
        printf("SD card format error: ");
        printf(fs_error_get_internal_desc(sd_app->info.status));
        printf("\r\n");
    } else {
        printf("SD card formatted\r\n");
    }
}

static void cli_sd_info(string_t args, void* _ctx) {
    SdApp* sd_app = (SdApp*)_ctx;

    const uint8_t str_buffer_size = 64;
    char str_buffer[str_buffer_size];

    // info vars
    uint32_t serial_num;
    SDError get_label_result, get_free_result;
    FATFS* fs;
    uint32_t free_clusters, free_sectors, total_sectors;
    char volume_label[34];

    // get fs info
    _fs_lock(&sd_app->info);
    get_label_result = f_getlabel(sd_app->info.path, volume_label, &serial_num);
    get_free_result = f_getfree(sd_app->info.path, &free_clusters, &fs);
    _fs_unlock(&sd_app->info);

    // calculate size
    total_sectors = (fs->n_fatent - 2) * fs->csize;
    free_sectors = free_clusters * fs->csize;
    uint16_t sector_size = _MAX_SS;
#if _MAX_SS != _MIN_SS
    sector_size = fs->ssize;
#endif

    // output info to dynamic strings
    if(get_label_result == SD_OK && get_free_result == SD_OK) {
        const char* fs_type = "";

        switch(fs->fs_type) {
        case(FS_FAT12):
            fs_type = "FAT12";
            break;
        case(FS_FAT16):
            fs_type = "FAT16";
            break;
        case(FS_FAT32):
            fs_type = "FAT32";
            break;
        case(FS_EXFAT):
            fs_type = "EXFAT";
            break;
        default:
            fs_type = "UNKNOWN";
            break;
        }

        snprintf(str_buffer, str_buffer_size, "Label: %s\r\n", volume_label);
        printf(str_buffer);

        snprintf(str_buffer, str_buffer_size, "%s, S/N: %lu\r\n", fs_type, serial_num);
        printf(str_buffer);

        snprintf(str_buffer, str_buffer_size, "Cluster: %d sectors\r\n", fs->csize);
        printf(str_buffer);

        snprintf(str_buffer, str_buffer_size, "Sector: %d bytes\r\n", sector_size);
        printf(str_buffer);

        snprintf(
            str_buffer, str_buffer_size, "%lu KB total\r\n", total_sectors / 1024 * sector_size);
        printf(str_buffer);

        snprintf(
            str_buffer, str_buffer_size, "%lu KB free\r\n", free_sectors / 1024 * sector_size);
        printf(str_buffer);
    } else {
        printf("SD status error: ");
        snprintf(
            str_buffer,
            str_buffer_size,
            "%s\r\n",
            fs_error_get_internal_desc(_fs_status(&sd_app->info)));
        printf(str_buffer);

        printf("Label error: ");
        snprintf(
            str_buffer, str_buffer_size, "%s\r\n", fs_error_get_internal_desc(get_label_result));
        printf(str_buffer);

        printf("Get free error: ");
        snprintf(
            str_buffer, str_buffer_size, "%s\r\n", fs_error_get_internal_desc(get_free_result));
        printf(str_buffer);
    }
}

int32_t sd_filesystem(void* p) {
    SdApp* sd_app = sd_app_alloc();
    FS_Api* fs_api = fs_api_alloc();

    Gui* gui = furi_record_open("gui");
    Cli* cli = furi_record_open("cli");
    ValueMutex* menu_vm = furi_record_open("menu");

    gui_add_view_port(gui, sd_app->view_port, GuiLayerFullscreen);
    gui_add_view_port(gui, sd_app->icon.view_port, GuiLayerStatusBarLeft);

    cli_add_command(cli, "sd_status", cli_sd_status, sd_app);
    cli_add_command(cli, "sd_format", cli_sd_format, sd_app);
    cli_add_command(cli, "sd_info", cli_sd_info, sd_app);

    // add api record
    furi_record_create("sdcard", fs_api);

    // init menu
    // TODO menu icon
    MenuItem* menu_item;
    menu_item = menu_item_alloc_menu("SD Card", assets_icons_get(I_SDcardMounted_11x8));

    menu_item_subitem_add(
        menu_item, menu_item_alloc_function("Info", NULL, app_sd_info_callback, sd_app));
    menu_item_subitem_add(
        menu_item, menu_item_alloc_function("Format", NULL, app_sd_format_callback, sd_app));
    menu_item_subitem_add(
        menu_item, menu_item_alloc_function("Eject", NULL, app_sd_eject_callback, sd_app));

    // add item to menu
    furi_check(menu_vm);
    with_value_mutex(
        menu_vm, (Menu * menu) { menu_item_add(menu, menu_item); });

    printf("[sd_filesystem] start\r\n");

    // add api record
    furi_record_create("sdcard", fs_api);

    // sd card cycle
    bool sd_was_present = true;

    // init detect pins
    hal_sd_detect_init();

    while(true) {
        if(sd_was_present) {
            if(hal_sd_detect()) {
                printf("[sd_filesystem] card detected\r\n");
                app_sd_mount_card(sd_app);

                if(sd_app->info.status != SD_OK) {
                    printf(
                        "[sd_filesystem] sd init error: %s\r\n",
                        fs_error_get_internal_desc(sd_app->info.status));
                    app_sd_notify_error();
                } else {
                    printf("[sd_filesystem] sd init ok\r\n");
                    app_sd_notify_success();
                }

                view_port_enabled_set(sd_app->icon.view_port, true);
                sd_was_present = false;

                if(!hal_sd_detect()) {
                    printf("[sd_filesystem] card removed\r\n");

                    view_port_enabled_set(sd_app->icon.view_port, false);
                    app_sd_unmount_card(sd_app);
                    sd_was_present = true;
                }
            }
        } else {
            if(!hal_sd_detect()) {
                printf("[sd_filesystem] card removed\r\n");

                view_port_enabled_set(sd_app->icon.view_port, false);
                app_sd_unmount_card(sd_app);
                sd_was_present = true;
                app_sd_notify_eject();
            }
        }

        delay(1000);
    }

    return 0;
}