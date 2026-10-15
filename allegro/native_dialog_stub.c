/*
 * native_dialog_stub.c: Allegro's native dialog addon needs GTK on Linux, which handhelds do not
 * have. Open Surge only uses it for error message boxes (printed to stderr instead), a file chooser
 * in the settings (reports "cancelled") and a text log (ignored). Built as
 * liballegro_dialog-static.a for the PortMaster build. zlib licence, like Allegro.
 */
#include <stdarg.h>
#include <stdio.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_native_dialog.h>

static bool initialized = false;

bool al_init_native_dialog_addon(void) { initialized = true; return true; }
bool al_is_native_dialog_addon_initialized(void) { return initialized; }
void al_shutdown_native_dialog_addon(void) { initialized = false; }
uint32_t al_get_allegro_native_dialog_version(void) { return ALLEGRO_VERSION_INT; }

int al_show_native_message_box(ALLEGRO_DISPLAY *display, char const *title, char const *heading,
                               char const *text, char const *buttons, int flags)
{
    (void)display; (void)buttons; (void)flags;
    fprintf(stderr, "[%s] %s\n%s\n", title ? title : "", heading ? heading : "", text ? text : "");
    return 0;
}

ALLEGRO_FILECHOOSER *al_create_native_file_dialog(char const *initial_path, char const *title,
                                                  char const *patterns, int mode)
{
    (void)initial_path; (void)title; (void)patterns; (void)mode;
    return NULL;
}
bool al_show_native_file_dialog(ALLEGRO_DISPLAY *display, ALLEGRO_FILECHOOSER *dialog)
{
    (void)display; (void)dialog;
    return false;
}
int al_get_native_file_dialog_count(const ALLEGRO_FILECHOOSER *dialog) { (void)dialog; return 0; }
const char *al_get_native_file_dialog_path(const ALLEGRO_FILECHOOSER *dialog, size_t index)
{
    (void)dialog; (void)index;
    return NULL;
}
void al_destroy_native_file_dialog(ALLEGRO_FILECHOOSER *dialog) { (void)dialog; }

ALLEGRO_TEXTLOG *al_open_native_text_log(char const *title, int flags)
{
    (void)title; (void)flags;
    return NULL;
}
void al_close_native_text_log(ALLEGRO_TEXTLOG *textlog) { (void)textlog; }
void al_append_native_text_log(ALLEGRO_TEXTLOG *textlog, char const *format, ...)
{
    (void)textlog; (void)format;
}
