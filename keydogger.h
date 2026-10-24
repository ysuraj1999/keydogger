#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <signal.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <limits.h>
#include <time.h>

#ifndef KEYDOGGER_H
#define KEYDOGGER_H

#define SLEEP_TIME 1000
#define LINUX_KEYS 50
#define MAX_KEYBOARDS 16

#define MAX_KEY_CODE 182

#define FLAG_UPPERCASE 128

// Errors
#define EOPEN 1  // Cannot open
#define EREAD 2  // Cannot read
#define EINVCH 3 // Invalid character
#define EINVC 4  // Invalid keycode
#define EINIT 5  // Error initializing
#define EADD 6   // Error adding
#define ESETUP 7
#define ECREATE 8
#define EWRITE 9
#define EENV 10
#define EFORK 12
#define ELEAD 13
#define ECHDIR 14
#define ERENAM 15
#define EPIPEFAIL 18
#define EUSAGE 19
#define ESTR 20
#define EUSER 21
#define ECLEAN 26

#define RC_PATH "keydoggerrc"
#define UINPUT_PATH "/dev/uinput"

struct key
{
    int position;
    char character;
    int keycode;
    bool is_shifted;
};

struct trie;

struct config
{
    char *path;
    struct trie *trie;
};

struct config_watch
{
    int inotify_fd;
    int watch_fd;
    bool lost;
    bool reload_pending;
    int64_t reload_deadline_ms;
};

struct pidfile
{
    int fd;
    char path[PATH_MAX];
};

struct keyboard
{
    int fd;
    char *path;
};

struct keyboard_set
{
    struct keyboard items[MAX_KEYBOARDS];
    size_t count;
    size_t live;
    bool configured;
};

struct trigger
{
    int active_fd;
    bool shifted;
    struct trie *cursor;
};

struct vkeyboard
{
    int fd;
    bool created;
};

struct keydogger
{
    struct config config;
    struct config_watch watch;
    struct pidfile pidfile;
    struct keyboard_set keyboards;
    struct trigger trigger;
    struct vkeyboard vkeyboard;
};

/* --- keymap --- */
void keymap_create(void);
bool keymap_code_is_supported(size_t code);
struct key keymap_key_from_char(char character);

/* --- text helpers --- */
bool text_is_printable_ascii(const char *string);
// TODO text trim can be internal
char *text_trim_whitespace(char *text);

/* --- trie --- */
struct trie *trie_create(void);
void trie_destroy(struct trie *trie);
void trie_insert(struct trie *root, const char *key, const char *expansion);
void trie_print(struct trie *root, size_t level);

/* --- config --- */
struct trie *config_parse(const char *path);
bool config_load(struct config *config, const char *path);
bool config_reload(struct config *config);
void config_unload(struct config *config);
void config_destroy(struct config *config);
bool config_is_loaded(const struct config *config);

/* --- config watch --- */
int64_t monotonic_now_ms(void);
bool config_watch_open(struct config_watch *watch, const char *path);
bool config_watch_rearm(struct config_watch *watch, const char *path);
void config_watch_drain(struct config_watch *watch);
bool config_watch_reload_is_due(const struct config_watch *watch);
bool config_watch_is_lost(const struct config_watch *watch);
void config_watch_close(struct config_watch *watch);

/* --- pidfile --- */
void pidfile_create(struct pidfile *pidfile);
void pidfile_destroy(struct pidfile *pidfile);
int pidfile_acquire(struct pidfile *pidfile, pid_t *running_pid);
int pidfile_acquire_wait(struct pidfile *pidfile, int timeout_ms);
void pidfile_write(struct pidfile *pidfile);
pid_t pidfile_read(struct pidfile *pidfile);
void pidfile_release(struct pidfile *pidfile);
bool pidfile_is_locked(const struct pidfile *pidfile);
bool pidfile_process_is_daemon(pid_t pid);
bool process_wait_exit(pid_t pid, int timeout_ms, const volatile sig_atomic_t *cancel);

/* --- keyboards --- */
void keyboards_configure(struct keyboard_set *keyboards);
bool keyboards_open(struct keyboard_set *keyboards);
void keyboards_close(struct keyboard_set *keyboards);
void keyboards_destroy(struct keyboard_set *keyboards);
void keyboards_disconnect(struct keyboard_set *keyboards, size_t index);
bool keyboards_are_configured(const struct keyboard_set *keyboards);
bool keyboards_are_open(const struct keyboard_set *keyboards);
bool keyboard_open(struct keyboard *keyboard);
void keyboard_close(struct keyboard *keyboard);
bool keyboard_read_event(struct keyboard *keyboard, struct input_event *event);

/* --- trigger --- */
void trigger_create(struct trigger *trigger);
void trigger_reset(struct trigger *trigger);
void trigger_cancel(struct trigger *trigger);
void trigger_switch_keyboard(struct trigger *trigger, int fd);
struct trie *trigger_feed(struct trigger *trigger, struct trie *root, int fd,
                          struct input_event event);
bool trigger_is_matching(const struct trigger *trigger);
bool trigger_has_active_keyboard(const struct trigger *trigger);

/* --- virtual keyboard --- */
bool vkeyboard_open(struct vkeyboard *vkeyboard);
bool vkeyboard_create(struct vkeyboard *vkeyboard);
void vkeyboard_destroy(struct vkeyboard *vkeyboard);
void vkeyboard_close(struct vkeyboard *vkeyboard);
bool vkeyboard_is_created(const struct vkeyboard *vkeyboard);
void vkeyboard_emit_text(struct vkeyboard *vkeyboard, const char *string);
void vkeyboard_emit_backspace(struct vkeyboard *vkeyboard, size_t n);
void vkeyboard_emit_shift_down(struct vkeyboard *vkeyboard);
void vkeyboard_emit_shift_up(struct vkeyboard *vkeyboard);

/* --- low-level evdev writes --- */
bool input_try_write_event(int fd, struct input_event event);
bool input_try_write_sync(int fd);
void input_write_event(int fd, struct input_event event);
void input_write_sync(int fd);

/* --- daemon --- */
void keydogger_create(struct keydogger *keydogger);
void daemon_install_signal_handlers(void);
void daemonize(struct keydogger *keydogger);
void daemon_run(struct keydogger *keydogger, int ready_fd);
void daemon_cleanup(struct keydogger *keydogger);
void daemon_reload_config(struct keydogger *keydogger);
void daemon_disconnect_keyboard(struct keydogger *keydogger, size_t index);
void daemon_expand(struct keydogger *keydogger, size_t keyboard_index,
                   struct trie *leaf, struct input_event event);
void signals_handle_termination(int sig);

/* --- cli --- */
void cli_usage(void);
void cli_start(struct keydogger *keydogger);
void cli_status(struct keydogger *keydogger);
void cli_stop(struct keydogger *keydogger);
void cli_restart(struct keydogger *keydogger);
void cli_debug(struct keydogger *keydogger);
void cli_viz(struct keydogger *keydogger);
void cli_stop_handle_sigint(int sig);

#endif
