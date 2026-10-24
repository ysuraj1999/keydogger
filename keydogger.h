#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <time.h>

#ifndef KEYDOGGER_H
#define KEYDOGGER_H

#define SLEEP_TIME 1000
#define READABLE_KEYS 126
#define LINUX_KEYS 50

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

static const size_t key_codes[] = {
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_0,
    KEY_MINUS,
    KEY_EQUAL,
    KEY_Q,
    KEY_W,
    KEY_E,
    KEY_R,
    KEY_T,
    KEY_Y,
    KEY_U,
    KEY_I,
    KEY_O,
    KEY_P,
    KEY_LEFTBRACE,
    KEY_RIGHTBRACE,
    KEY_A,
    KEY_S,
    KEY_D,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_SEMICOLON,
    KEY_APOSTROPHE,
    KEY_GRAVE,
    KEY_BACKSLASH,
    KEY_Z,
    KEY_X,
    KEY_C,
    KEY_V,
    KEY_B,
    KEY_N,
    KEY_M,
    KEY_COMMA,
    KEY_DOT,
    KEY_SLASH,
    KEY_SPACE,
    KEY_LEFTSHIFT,
    KEY_RIGHTSHIFT,
};

struct trie
{
    struct trie *next[READABLE_KEYS];
    char character;
    int keycode;
    bool is_leaf;
    bool is_shifted;
    struct trie *parent;
    char *expansion;
    size_t size;
};

struct key
{
    int position;
    char character;
    int keycode;
    bool is_shifted;
};

bool pid_is_daemon(pid_t pid);
void pidfile_path_init(void);
pid_t pidfile_read(void);
int pidfile_acquire(pid_t *running_pid);
int pidfile_acquire_wait(int timeout_ms);
void pidfile_write(void);
void pidfile_release(void);
void sigint_handler(int sig);
bool pid_exit_wait(pid_t pid, int timeout_ms);
bool try_send_key_to_device(int fd, struct input_event event);
bool try_send_sync(int fd);
bool is_printable_ascii(char *string);
void keyboard_disconnect(size_t index);
char *trim_whitespace(char *text);

void trie_cleanup(struct trie *trie);
void cleanup(void);
struct trie *rc_parse(const char *path);
void rc_load(const char *path);
bool rc_reload(void);
bool config_watch_init(void);
void config_watch_drain(void);
int64_t now_ms(void);
bool is_key_code_supported(size_t code);
void send_key_to_device(int keyboard_device, struct input_event event);
void send_backspace(int device_fd, size_t n);
void send_sync(int device_fd);
void send_shift_down(int device_fd);
void send_shift_up(int device_fd);
void send_to_keyboard(char *string);
void trie_init(struct trie *trie, struct key *key);
struct key get_key_from_char(char character);
void trie_push(struct trie *root, char *key, char *expansion);
void init_virtual_device(int vkeyboard_device);
void signal_cleanup_handler(int sig);
void keydogger_daemon(void);
void daemonize_keydogger(void);
void print_trie(struct trie *trie, size_t level);
void read_keyboard_env(void);
void init_key_to_position_map(void);

#endif
