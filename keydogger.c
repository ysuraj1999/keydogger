#include <stdio.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>

#include "keydogger.h"

#define STARTUP_TIMEOUT_MS 5000
#define SHUTDOWN_TIMEOUT_MS 5000

#if DEBUG_MODE == 1
static char *KEYBOARD_DEVICE = KEYDOGGER_KEYBOARD;
#else
static char *KEYBOARD_DEVICE = NULL;
#endif

static char *DAEMON = "keydoggerd";
static struct trie *TRIE = NULL;

char *DEBUG_RC_PATH = "./keydoggerrc";

static int fkeyboard_device = -1;
static int vkeyboard_device = -1;
static int ready_fd = -1;
static int pidfile_fd = -1;
static char pidfile_path[PATH_MAX];
static volatile sig_atomic_t cancel_wait = 0;

// Direct mapping of char codes to linux key codes
static int char_codes_to_key_codes[] = {
    // 0 -> 33
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    KEY_SPACE,
    KEY_1 | FLAG_UPPERCASE,
    KEY_APOSTROPHE | FLAG_UPPERCASE,
    KEY_3 | FLAG_UPPERCASE,
    KEY_4 | FLAG_UPPERCASE,
    KEY_5 | FLAG_UPPERCASE,
    KEY_7 | FLAG_UPPERCASE,
    KEY_APOSTROPHE,
    KEY_9 | FLAG_UPPERCASE,
    KEY_0 | FLAG_UPPERCASE,
    KEY_8 | FLAG_UPPERCASE,
    KEY_EQUAL | FLAG_UPPERCASE,
    KEY_COMMA,
    KEY_MINUS,
    KEY_DOT,
    KEY_SLASH,
    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_SEMICOLON | FLAG_UPPERCASE,
    KEY_SEMICOLON,
    KEY_COMMA | FLAG_UPPERCASE,
    KEY_EQUAL,
    KEY_DOT | FLAG_UPPERCASE,
    KEY_SLASH | FLAG_UPPERCASE,
    KEY_2 | FLAG_UPPERCASE,
    KEY_A | FLAG_UPPERCASE,
    KEY_B | FLAG_UPPERCASE,
    KEY_C | FLAG_UPPERCASE,
    KEY_D | FLAG_UPPERCASE,
    KEY_E | FLAG_UPPERCASE,
    KEY_F | FLAG_UPPERCASE,
    KEY_G | FLAG_UPPERCASE,
    KEY_H | FLAG_UPPERCASE,
    KEY_I | FLAG_UPPERCASE,
    KEY_J | FLAG_UPPERCASE,
    KEY_K | FLAG_UPPERCASE,
    KEY_L | FLAG_UPPERCASE,
    KEY_M | FLAG_UPPERCASE,
    KEY_N | FLAG_UPPERCASE,
    KEY_O | FLAG_UPPERCASE,
    KEY_P | FLAG_UPPERCASE,
    KEY_Q | FLAG_UPPERCASE,
    KEY_R | FLAG_UPPERCASE,
    KEY_S | FLAG_UPPERCASE,
    KEY_T | FLAG_UPPERCASE,
    KEY_U | FLAG_UPPERCASE,
    KEY_V | FLAG_UPPERCASE,
    KEY_W | FLAG_UPPERCASE,
    KEY_X | FLAG_UPPERCASE,
    KEY_Y | FLAG_UPPERCASE,
    KEY_Z | FLAG_UPPERCASE,
    KEY_LEFTBRACE,
    KEY_BACKSLASH,
    KEY_RIGHTBRACE,
    KEY_6 | FLAG_UPPERCASE,
    KEY_MINUS | FLAG_UPPERCASE,
    KEY_GRAVE,
    KEY_A,
    KEY_B,
    KEY_C,
    KEY_D,
    KEY_E,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_I,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_M,
    KEY_N,
    KEY_O,
    KEY_P,
    KEY_Q,
    KEY_R,
    KEY_S,
    KEY_T,
    KEY_U,
    KEY_V,
    KEY_W,
    KEY_X,
    KEY_Y,
    KEY_Z,
    KEY_LEFTBRACE | FLAG_UPPERCASE,
    KEY_BACKSLASH | FLAG_UPPERCASE,
    KEY_RIGHTBRACE | FLAG_UPPERCASE,
    KEY_GRAVE | FLAG_UPPERCASE,
};

static int key_codes_to_position[MAX_KEY_CODE];

void init_key_to_position_map()
{
    memset(key_codes_to_position, -1, sizeof(key_codes_to_position));
    key_codes_to_position[KEY_SPACE] = 31;
    key_codes_to_position[KEY_1 | FLAG_UPPERCASE] = 32;
    key_codes_to_position[KEY_APOSTROPHE | FLAG_UPPERCASE] = 33;
    key_codes_to_position[KEY_3 | FLAG_UPPERCASE] = 34;
    key_codes_to_position[KEY_4 | FLAG_UPPERCASE] = 35;
    key_codes_to_position[KEY_5 | FLAG_UPPERCASE] = 36;
    key_codes_to_position[KEY_7 | FLAG_UPPERCASE] = 37;
    key_codes_to_position[KEY_APOSTROPHE] = 38;
    key_codes_to_position[KEY_9 | FLAG_UPPERCASE] = 39;
    key_codes_to_position[KEY_0 | FLAG_UPPERCASE] = 40;
    key_codes_to_position[KEY_8 | FLAG_UPPERCASE] = 41;
    key_codes_to_position[KEY_EQUAL | FLAG_UPPERCASE] = 42;
    key_codes_to_position[KEY_COMMA] = 43;
    key_codes_to_position[KEY_MINUS] = 44;
    key_codes_to_position[KEY_DOT] = 45;
    key_codes_to_position[KEY_SLASH] = 46;
    key_codes_to_position[KEY_0] = 47;
    key_codes_to_position[KEY_1] = 48;
    key_codes_to_position[KEY_2] = 49;
    key_codes_to_position[KEY_3] = 50;
    key_codes_to_position[KEY_4] = 51;
    key_codes_to_position[KEY_5] = 52;
    key_codes_to_position[KEY_6] = 53;
    key_codes_to_position[KEY_7] = 54;
    key_codes_to_position[KEY_8] = 55;
    key_codes_to_position[KEY_9] = 56;
    key_codes_to_position[KEY_SEMICOLON | FLAG_UPPERCASE] = 57;
    key_codes_to_position[KEY_SEMICOLON] = 58;
    key_codes_to_position[KEY_COMMA | FLAG_UPPERCASE] = 59;
    key_codes_to_position[KEY_EQUAL] = 60;
    key_codes_to_position[KEY_DOT | FLAG_UPPERCASE] = 61;
    key_codes_to_position[KEY_SLASH | FLAG_UPPERCASE] = 62;
    key_codes_to_position[KEY_2 | FLAG_UPPERCASE] = 63;
    key_codes_to_position[KEY_A | FLAG_UPPERCASE] = 64;
    key_codes_to_position[KEY_B | FLAG_UPPERCASE] = 65;
    key_codes_to_position[KEY_C | FLAG_UPPERCASE] = 66;
    key_codes_to_position[KEY_D | FLAG_UPPERCASE] = 67;
    key_codes_to_position[KEY_E | FLAG_UPPERCASE] = 68;
    key_codes_to_position[KEY_F | FLAG_UPPERCASE] = 69;
    key_codes_to_position[KEY_G | FLAG_UPPERCASE] = 70;
    key_codes_to_position[KEY_H | FLAG_UPPERCASE] = 71;
    key_codes_to_position[KEY_I | FLAG_UPPERCASE] = 72;
    key_codes_to_position[KEY_J | FLAG_UPPERCASE] = 73;
    key_codes_to_position[KEY_K | FLAG_UPPERCASE] = 74;
    key_codes_to_position[KEY_L | FLAG_UPPERCASE] = 75;
    key_codes_to_position[KEY_M | FLAG_UPPERCASE] = 76;
    key_codes_to_position[KEY_N | FLAG_UPPERCASE] = 77;
    key_codes_to_position[KEY_O | FLAG_UPPERCASE] = 78;
    key_codes_to_position[KEY_P | FLAG_UPPERCASE] = 79;
    key_codes_to_position[KEY_Q | FLAG_UPPERCASE] = 80;
    key_codes_to_position[KEY_R | FLAG_UPPERCASE] = 81;
    key_codes_to_position[KEY_S | FLAG_UPPERCASE] = 82;
    key_codes_to_position[KEY_T | FLAG_UPPERCASE] = 83;
    key_codes_to_position[KEY_U | FLAG_UPPERCASE] = 84;
    key_codes_to_position[KEY_V | FLAG_UPPERCASE] = 85;
    key_codes_to_position[KEY_W | FLAG_UPPERCASE] = 86;
    key_codes_to_position[KEY_X | FLAG_UPPERCASE] = 87;
    key_codes_to_position[KEY_Y | FLAG_UPPERCASE] = 88;
    key_codes_to_position[KEY_Z | FLAG_UPPERCASE] = 89;
    key_codes_to_position[KEY_LEFTBRACE] = 90;
    key_codes_to_position[KEY_BACKSLASH] = 91;
    key_codes_to_position[KEY_RIGHTBRACE] = 92;
    key_codes_to_position[KEY_6 | FLAG_UPPERCASE] = 93;
    key_codes_to_position[KEY_MINUS | FLAG_UPPERCASE] = 94;
    key_codes_to_position[KEY_GRAVE] = 95;
    key_codes_to_position[KEY_A] = 96;
    key_codes_to_position[KEY_B] = 97;
    key_codes_to_position[KEY_C] = 98;
    key_codes_to_position[KEY_D] = 99;
    key_codes_to_position[KEY_E] = 100;
    key_codes_to_position[KEY_F] = 101;
    key_codes_to_position[KEY_G] = 102;
    key_codes_to_position[KEY_H] = 103;
    key_codes_to_position[KEY_I] = 104;
    key_codes_to_position[KEY_J] = 105;
    key_codes_to_position[KEY_K] = 106;
    key_codes_to_position[KEY_L] = 107;
    key_codes_to_position[KEY_M] = 108;
    key_codes_to_position[KEY_N] = 109;
    key_codes_to_position[KEY_O] = 110;
    key_codes_to_position[KEY_P] = 111;
    key_codes_to_position[KEY_Q] = 112;
    key_codes_to_position[KEY_R] = 113;
    key_codes_to_position[KEY_S] = 114;
    key_codes_to_position[KEY_T] = 115;
    key_codes_to_position[KEY_U] = 116;
    key_codes_to_position[KEY_V] = 117;
    key_codes_to_position[KEY_W] = 118;
    key_codes_to_position[KEY_X] = 119;
    key_codes_to_position[KEY_Y] = 120;
    key_codes_to_position[KEY_Z] = 121;
    key_codes_to_position[KEY_LEFTBRACE | FLAG_UPPERCASE] = 122;
    key_codes_to_position[KEY_BACKSLASH | FLAG_UPPERCASE] = 123;
    key_codes_to_position[KEY_RIGHTBRACE | FLAG_UPPERCASE] = 124;
    key_codes_to_position[KEY_GRAVE | FLAG_UPPERCASE] = 125;
}

void trie_cleanup(struct trie *trie)
{
    if (trie == NULL)
    {
        return;
    }
    else
    {
        free(trie->expansion);
        for (size_t i = 0; i < READABLE_KEYS; i++)
        {
            trie_cleanup(trie->next[i]);
        }
        free(trie);
    }
}

static bool pid_is_daemon(pid_t pid)
{
    if (pid <= 0)
    {
        return false;
    }
    if (kill(pid, 0) < 0 && errno != EPERM)
    {
        return false;
    }

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE *comm_file = fopen(path, "r");
    if (comm_file == NULL)
    {
        return true;
    }

    char comm[32] = {0};
    bool has_comm = fgets(comm, sizeof(comm), comm_file) != NULL;
    fclose(comm_file);
    if (!has_comm)
    {
        return true;
    }

    comm[strcspn(comm, "\n")] = '\0';
    return strcmp(comm, DAEMON) == 0;
}

static void pidfile_path_init()
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == NULL || runtime_dir[0] == '\0')
    {
        fprintf(stderr, "XDG_RUNTIME_DIR is not set\n");
        exit(EXIT_FAILURE);
    }
    if (runtime_dir[0] != '/')
    {
        fprintf(stderr, "XDG_RUNTIME_DIR must be an absolute path\n");
        exit(EXIT_FAILURE);
    }

    struct stat runtime_stat;
    if (stat(runtime_dir, &runtime_stat) < 0)
    {
        perror("XDG_RUNTIME_DIR");
        exit(EXIT_FAILURE);
    }
    if (!S_ISDIR(runtime_stat.st_mode))
    {
        fprintf(stderr, "XDG_RUNTIME_DIR is not a directory\n");
        exit(EXIT_FAILURE);
    }
    if (access(runtime_dir, R_OK | W_OK | X_OK) < 0)
    {
        perror("XDG_RUNTIME_DIR");
        exit(EXIT_FAILURE);
    }

    int length = snprintf(pidfile_path, sizeof(pidfile_path),
                    "%s/keydogger.pid", runtime_dir);
    if (length < 0 || (size_t)length >= sizeof(pidfile_path))
    {
        fprintf(stderr, "XDG_RUNTIME_DIR is too long\n");
        exit(EXIT_FAILURE);
    }
}

static pid_t pidfile_read()
{
    int fd = open(pidfile_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        return -1;
    }

    char buffer[32] = {0};
    ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (length <= 0)
    {
        return -1;
    }

    pid_t pid = (pid_t)strtol(buffer, NULL, 10);
    if (pid_is_daemon(pid))
    {
        return pid;
    }
    return -1;
}

// Returns 0 on success, 1 if already running, -1 on error
static int pidfile_acquire(pid_t *running_pid)
{
    int fd = open(pidfile_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) < 0)
    {
        if (errno == EWOULDBLOCK)
        {
            if (running_pid != NULL)
            {
                char buffer[32] = {0};
                ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
                if (length > 0)
                {
                    *running_pid = (pid_t)strtol(buffer, NULL, 10);
                }
            }
            close(fd);
            return 1;
        }
        close(fd);
        return -1;
    }

    ftruncate(fd, 0);
    lseek(fd, 0, SEEK_SET);

    pidfile_fd = fd;
    return 0;
}

static int pidfile_acquire_wait(int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 100)
    {
        int status = pidfile_acquire(NULL);
        if (status == 0)
        {
            return 0;
        }
        if (status < 0)
        {
            return -1;
        }
        usleep(100 * 1000);
    }
    return -1;
}

static void pidfile_write()
{
    if (pidfile_fd < 0)
    {
        return;
    }
    if (ftruncate(pidfile_fd, 0) == 0)
    {
        lseek(pidfile_fd, 0, SEEK_SET);
        dprintf(pidfile_fd, "%d\n", getpid());
    }
}

static void pidfile_release()
{
    if (pidfile_fd < 0)
    {
        return;
    }
    close(pidfile_fd);
    pidfile_fd = -1;
}

static void sigint_handler(int sig)
{
    (void)sig;
    cancel_wait = 1;
}

static bool pid_exit_wait(pid_t pid, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 50)
    {
        if (kill(pid, 0) < 0 && errno == ESRCH)
        {
            return true;
        }
        if (cancel_wait)
        {
            return false;
        }
        usleep(50 * 1000);
    }
    return kill(pid, 0) < 0 && errno == ESRCH;
}

void cleanup()
{
    if (vkeyboard_device >= 0)
    {
        ioctl(vkeyboard_device, UI_DEV_DESTROY);
        close(vkeyboard_device);
        vkeyboard_device = -1;
    }

    if (fkeyboard_device >= 0)
    {
        close(fkeyboard_device);
        fkeyboard_device = -1;
    }

    trie_cleanup(TRIE);
    TRIE = NULL;

    pidfile_release();
}

static bool is_printable_ascii(char *string)
{
    for (size_t i = 0; string[i] != '\0'; i++)
    {
        unsigned char character = (unsigned char)string[i];
        if (character < 32 || character > 126)
            return false;
    }
    return true;
}

void rc_load(const char *path)
{
    char rc_file_path[256];
    int path_length;

    if (path != NULL)
    {
        path_length = snprintf(rc_file_path, sizeof(rc_file_path), "%s", path);
        if (path_length < 0 || (size_t)path_length >= sizeof(rc_file_path))
        {
            fprintf(stderr, "Config path too long\n");
            exit(ESTR);
        }
    }
    else
    {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0')
        {
            struct passwd *passwd_entry = getpwuid(getuid());
            if (passwd_entry != NULL)
            {
                home = passwd_entry->pw_dir;
            }
        }
        if (home == NULL || home[0] == '\0')
        {
            fprintf(stderr, "Error determining home directory\n");
            exit(EUSER);
        }

        path_length = snprintf(rc_file_path, sizeof(rc_file_path), "%s/%s", home, RC_PATH);
        if (path_length < 0 || (size_t)path_length >= sizeof(rc_file_path))
        {
            fprintf(stderr, "Error constructing config path %s\n", RC_PATH);
            exit(ESTR);
        }
    }

    TRIE = malloc(sizeof(struct trie));
    trie_init(TRIE, NULL);
    FILE *rc_file = fopen(rc_file_path, "r");
    if (rc_file == NULL)
    {
        fprintf(stderr, "Error opening %s\n", rc_file_path);
        exit(EOPEN);
    }

    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t line_length;
    while ((line_length = getline(&line, &line_capacity, rc_file)) != -1)
    {
        // Strip trailing newline and carriage return
        while (line_length > 0 &&
            (line[line_length - 1] == '\n' || line[line_length - 1] == '\r'))
        {
            line[--line_length] = '\0';
        }

        // Split into trigger and expansion
        char *separator = strchr(line, '=');
        if (separator == NULL)
        {
            continue;
        }
        *separator = '\0';

        char *key = line;
        char *value = separator + 1;
        if (*key == '\0')
        {
            continue;
        }

        if (!is_printable_ascii(key) || !is_printable_ascii(value))
        {
            fprintf(stderr, "Warning: skipping non-ASCII expansion for trigger %s\n", key);
            continue;
        }
        trie_push(key, value);
    }
    free(line);
    fclose(rc_file);
}

bool is_key_code_supported(size_t code)
{
    // 2 -> 13 = 1 -> =
    if (code >= KEY_1 && code <= KEY_EQUAL)
        return true;
    // 16 -> 27 = q -> ]
    if (code >= KEY_Q && code <= KEY_RIGHTBRACE)
        return true;
    // 30 -> 40, 41, 42 = a -> ', `, leftshift
    if (code >= KEY_A && code <= KEY_LEFTSHIFT)
        return true;
    // 43 -> 54 = backslash -> rightshift
    if (code >= KEY_BACKSLASH && code <= KEY_RIGHTSHIFT)
        return true;
    // 57 -> space
    if (code == KEY_SPACE)
        return true;
    return false;
}

struct key get_key_from_char(char character)
{
    struct key key = {0};
    key.character = character;
    key.is_shifted = false;
    int position = (int)character - 1;
    int keycode = char_codes_to_key_codes[position];
    if (keycode & FLAG_UPPERCASE)
    {
        key.is_shifted = true;
        // subtract the flag to just get the keycode
        keycode &= ~FLAG_UPPERCASE;
    }
    key.keycode = keycode;
    key.position = position;
    return key;
}

void send_key_to_device(int keyboard_device, struct input_event event)
{
    int write_status = write(keyboard_device, &event, sizeof(event));
    if (write_status < 0)
    {
        fprintf(stderr, "Error writing to virtual device\n");
        exit(EWRITE);
    }
}

void send_backspace(int device_fd, size_t n)
{
    struct input_event event = {0};
    event.code = KEY_BACKSPACE;
    event.type = EV_KEY;
    for (size_t i = 0; i < n; i++)
    {
        event.value = 1;
        send_key_to_device(device_fd, event);
        event.value = 0;
        send_key_to_device(device_fd, event);
        send_sync(device_fd);
    }
}

void send_sync(int device_fd)
{
    struct input_event event = {0};
    event.type = EV_SYN;
    event.value = SYN_REPORT;
    send_key_to_device(device_fd, event);
}

void send_shift_down(int device_fd)
{
    struct input_event event = {0};
    event.type = EV_KEY;
    event.code = KEY_LEFTSHIFT;
    event.value = 1;
    send_key_to_device(device_fd, event);
    usleep(SLEEP_TIME);
}

void send_shift_up(int device_fd)
{
    struct input_event event = {0};
    event.type = EV_KEY;
    event.code = KEY_LEFTSHIFT;
    event.value = 0;
    send_key_to_device(device_fd, event);
    usleep(SLEEP_TIME);
}

void send_to_keyboard(char *string)
{
    struct input_event event = {0};
    event.type = EV_KEY;
    for (size_t i = 0; i < strlen(string); i++)
    {
        struct key key = get_key_from_char(string[i]);
        event.code = key.keycode;
        event.value = 1;
        if (key.is_shifted)
            send_shift_down(vkeyboard_device);
        send_key_to_device(vkeyboard_device, event);
        usleep(SLEEP_TIME);
        event.value = 0;
        send_key_to_device(vkeyboard_device, event);
        usleep(SLEEP_TIME);
        if (key.is_shifted)
            send_shift_up(vkeyboard_device);
    }
    send_sync(vkeyboard_device);
}

void trie_init(struct trie *trie, struct key *key)
{
    trie->character = '\0';
    trie->is_shifted = false;
    trie->parent = NULL;
    trie->is_leaf = false;
    trie->expansion = NULL;
    trie->size = 0;
    if (key != NULL)
    {
        trie->character = key->character;
        trie->keycode = key->keycode;
        trie->is_shifted = key->is_shifted;
    }
    for (size_t i = 0; i < READABLE_KEYS; i++)
    {
        trie->next[i] = NULL;
    }
}

void trie_push(char *key, char *expansion)
{
    struct trie *current_trie = TRIE;
    for (size_t i = 0; i < strlen(key); i++)
    {
        char character = key[i];
        struct key key = get_key_from_char(character);
        int position = key.position;
        if (current_trie->next[position] == NULL)
        {
            current_trie->next[position] = malloc(sizeof(struct trie));
            trie_init(current_trie->next[position], &key);
            current_trie->next[position]->size = current_trie->size + 1;
            current_trie->next[position]->parent = current_trie;
        }
        current_trie = current_trie->next[position];
    }
    current_trie->is_leaf = true;
    current_trie->expansion = malloc(strlen(expansion) + 1);
    strcpy(current_trie->expansion, expansion);
}

void init_virtual_device(int vkeyboard_device)
{
    struct uinput_setup usetup = {0};

    int status;
    // setup as keyboard
    if ((status = ioctl(vkeyboard_device, UI_SET_EVBIT, EV_KEY)) < 0)
    {
        fprintf(stderr, "Error initializing virtual input\n");
        exit(EINIT);
    }
    // setup keys to emit
    if ((status = ioctl(vkeyboard_device, UI_SET_KEYBIT, KEY_BACKSPACE)) < 0)
    {
        fprintf(stderr, "Error adding key to virtual input : %d\n", KEY_BACKSPACE);
        exit(EADD);
    }
    if ((status = ioctl(vkeyboard_device, UI_SET_KEYBIT, KEY_LEFTCTRL)) < 0)
    {
        fprintf(stderr, "Error adding key to virtual input : %d\n", KEY_LEFTCTRL);
        exit(EADD);
    }
    for (size_t i = 0; i < LINUX_KEYS; i++)
    {
        if ((status = ioctl(vkeyboard_device, UI_SET_KEYBIT, key_codes[i])) < 0)
        {
            fprintf(stderr, "Error adding key to virtual input : %ld\n", key_codes[i]);
            exit(EADD);
        }
    }

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 1187;
    usetup.id.product = 1999;
    strcpy(usetup.name, "keydogger");

    if ((status = ioctl(vkeyboard_device, UI_DEV_SETUP, &usetup)) < 0)
    {
        fprintf(stderr, "Error setting up virtual device\n");
        exit(ESETUP);
    }
    if ((status = ioctl(vkeyboard_device, UI_DEV_CREATE)) < 0)
    {
        fprintf(stderr, "Error creating up virtual device\n");
        exit(ECREATE);
    };
}

void signal_cleanup_handler(int sig __attribute__((unused)))
{
    cleanup();
    exit(0);
}

void keydogger_daemon()
{
    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_cleanup_handler;
    sigfillset(&sa.sa_mask); // Block all signals during handler execution

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        fprintf(stderr, "Error cleaning up for SIGINT\n");
        exit(ECLEAN);
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        fprintf(stderr, "Error cleaning up for SIGTERM\n");
        exit(ECLEAN);
    }

    fkeyboard_device = open(KEYBOARD_DEVICE, O_RDWR | O_APPEND, NULL);
    bool is_shifted = false;

    if (fkeyboard_device < 0)
    {
        fprintf(stderr, "Error opening %s\n", KEYBOARD_DEVICE);
        exit(EOPEN);
    }
    vkeyboard_device = open(UINPUT_PATH, O_WRONLY);
    if (vkeyboard_device < 0)
    {
        fprintf(stderr, "Error opening %s\n", UINPUT_PATH);
        exit(EOPEN);
    }

    init_virtual_device(vkeyboard_device);

    init_key_to_position_map();

    pidfile_write();

    // Let the parent process know that startup succeeded
    if (ready_fd >= 0)
    {
        char ready_message[64];
        int ready_length = snprintf(ready_message, sizeof(ready_message),
                                    "keydoggerd started at pid %d\n", getpid());
        if (ready_length > 0)
        {
            size_t ready_size = (size_t)ready_length;
            if (ready_size >= sizeof(ready_message))
            {
                ready_size = sizeof(ready_message) - 1;
            }
            write(ready_fd, ready_message, ready_size);
        }
        close(ready_fd);
        ready_fd = -1;
    }

    struct input_event event = {0};
    struct trie *current_trie = TRIE;
    while (1)
    {
        int read_inputs = read(fkeyboard_device, &event, sizeof(struct input_event));
        if (read_inputs < 0)
        {
            fprintf(stderr, "Error reading from %s\n", KEYBOARD_DEVICE);
            exit(EREAD);
        }
        // ignore events other than key
        if (event.type != EV_KEY)
        {
            continue;
        }
        // ignore if keydogger doesnt recognize the key
        if (is_key_code_supported(event.code) == false)
        {
            continue;
        }

        // Handle shift down
        if (event.value == 1 && (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT))
        {
            is_shifted = true;
            continue;
        }

        // Handle shift up
        if (event.value == 0 && (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT))
        {
            is_shifted = false;
            continue;
        }

        // ignore shift keys
        if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)
        {
            continue;
        }

        // ignore key up event
        if (event.value == 0)
        {
            continue;
        }

        int event_code;
        if (is_shifted)
            event_code = event.code | FLAG_UPPERCASE;
        else
            event_code = event.code;
        int position = key_codes_to_position[event_code];

        // if next doesnt match trigger, reset
        if (current_trie->next[position] == NULL)
        {
            current_trie = TRIE;
            continue;
        }

        struct trie *next = current_trie->next[position];
        // if next doesnt match trigger, reset
        if ((event_code ^ char_codes_to_key_codes[position]) != 0)
        {
            current_trie = TRIE;
            continue;
        }
        // if next is terminal, expand it
        if (next->is_leaf)
        {
            struct trie *next_copy = next;
            // RELEASE ALL TRIGGER KEYS
            event.value = 0;
            // release last key
            send_key_to_device(fkeyboard_device, event);
            send_sync(fkeyboard_device);

            event.code = KEY_LEFTSHIFT;
            send_key_to_device(fkeyboard_device, event);

            send_sync(fkeyboard_device);
            event.code = KEY_RIGHTSHIFT;

            send_key_to_device(fkeyboard_device, event);
            send_sync(fkeyboard_device);

            while (next_copy->parent != NULL)
            {
                event.code = next_copy->parent->keycode;
                send_key_to_device(fkeyboard_device, event);
                send_sync(fkeyboard_device);
                next_copy = next_copy->parent;
            }

            // EXPAND
            send_backspace(vkeyboard_device, next->size);
            send_to_keyboard(next->expansion);

            // Reset trie head
            current_trie = TRIE;
        }
        else
        {
            current_trie = next;
        }
    }
}

void daemonize_keydogger()
{
    int ready_pipe[2];
    pid_t pid;
    pid_t sid;
    sigset_t sigset;

    if (pipe(ready_pipe) < 0)
    {
        perror("Error creating startup pipe");
        exit(EPIPEFAIL);
    }

    fflush(NULL);

    if ((pid = fork()) < 0)
    {
        fprintf(stderr, "Error forking 1\n");
        exit(EFORK);
    }

    // Parent waits until the daemon has finished initializing
    if (pid > 0)
    {
        close(ready_pipe[1]);

        struct pollfd poll_fd = {0};
        poll_fd.fd = ready_pipe[0];
        poll_fd.events = POLLIN;

        int poll_status;
        do
        {
            poll_status = poll(&poll_fd, 1, STARTUP_TIMEOUT_MS);
        } while (poll_status < 0 && errno == EINTR);

        if (poll_status == 0)
        {
            fprintf(stderr, "Timed out waiting for keydoggerd to start\n");
            exit(EXIT_FAILURE);
        }
        if (poll_status < 0)
        {
            perror("Error waiting for keydoggerd");
            exit(EXIT_FAILURE);
        }

        char message[256];
        ssize_t message_length;
        do
        {
            message_length = read(ready_pipe[0], message, sizeof(message) - 1);
        } while (message_length < 0 && errno == EINTR);

        if (message_length <= 0)
        {
            fprintf(stderr, "keydoggerd failed to start\n");
            exit(EXIT_FAILURE);
        }

        message[message_length] = '\0';
        printf("%s", message);
        exit(EXIT_SUCCESS);
    }

    // First child detaches from the controlling terminal
    close(ready_pipe[0]);

    if ((sid = setsid()) < 0)
    {
        fprintf(stderr, "Error upgrading to session leader\n");
        exit(ELEAD);
    }

    if ((pid = fork()) < 0)
    {
        fprintf(stderr, "Error forking 2\n");
        exit(EFORK);
    }
    // exit Child1
    if (pid > 0)
    {
        _exit(EXIT_SUCCESS);
    }

    // Final daemon process
    ready_fd = ready_pipe[1];

    if (prctl(PR_SET_NAME, DAEMON) < 0)
    {
        fprintf(stderr, "Error setting name for process\n");
        exit(ERENAM);
    }

    // Close inherited descriptors, keeping the readiness pipe
    long max_fd = sysconf(_SC_OPEN_MAX);
    for (long open_fd = max_fd - 1; open_fd > STDERR_FILENO; open_fd--)
    {
        if (open_fd != ready_fd && open_fd != pidfile_fd)
        {
            close((int)open_fd);
        }
    }

    for (int i = 1; i < _NSIG; i++)
    {
        if (i != SIGKILL && i != SIGSTOP)
            signal(i, SIG_DFL);
    }

    sigemptyset(&sigset);
    sigprocmask(SIG_SETMASK, &sigset, NULL);

    if (chdir("/") < 0)
    {
        fprintf(stderr, "Error changing directory to /\n");
        exit(ECHDIR);
    }

    keydogger_daemon();
}

void print_trie(struct trie *trie, size_t level)
{
    if (level == 0)
        printf("|");
    if (trie == NULL)
        return;
    for (size_t i = 0; i < level; i++)
    {
        printf("-");
    }
    printf("%c", trie->character);
    if (trie->is_leaf)
    {
        printf(" = %s\n", trie->expansion);
    }
    else
    {
        printf("\n");
    }
    for (size_t i = 0; i < READABLE_KEYS; i++)
    {
        print_trie(trie->next[i], level + 1);
    }
}

void read_keyboard_env()
{
#if DEBUG_MODE == 0
    KEYBOARD_DEVICE = getenv("KEYDOGGER_KEYBOARD");
    if (KEYBOARD_DEVICE == NULL)
    {
        fprintf(stderr, "Required environment variable missing - KEYDOGGER_KEYBOARD\n");
        exit(EENV);
    }
#endif
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage error: keydogger start | stop | status | debug | viz\n");
        exit(EUSAGE);
    }

    if (strcmp(argv[1], "start") == 0)
    {
        pidfile_path_init();

        pid_t running_pid = 0;
        int pidfile_status = pidfile_acquire(&running_pid);
        if (pidfile_status == 1)
        {
            if (running_pid > 0)
            {
                printf("Already running at pid %d\n", running_pid);
            }
            else
            {
                printf("Already running\n");
            }
            exit(EXIT_SUCCESS);
        }
        if (pidfile_status < 0)
        {
            perror("Error opening pidfile");
            exit(EOPEN);
        }
        read_keyboard_env();
        rc_load(NULL);
        daemonize_keydogger();
    }
    else if (strcmp(argv[1], "status") == 0)
    {
        pidfile_path_init();

        pid_t pid = pidfile_read();
        if (pid > 0)
        {
            printf("keydogger running at pid %d\n", pid);
        }
        else
        {
            printf("Not running\n");
        }
        exit(EXIT_SUCCESS);
    }
    else if (strcmp(argv[1], "stop") == 0)
    {
        pidfile_path_init();

        pid_t pid = pidfile_read();
        if (pid <= 0)
        {
            printf("Not running\n");
            exit(EXIT_SUCCESS);
        }
        if (kill(pid, SIGTERM) < 0)
        {
            if (errno == ESRCH)
            {
                printf("keydoggerd already exited (pid %d)\n", pid);
                exit(EXIT_SUCCESS);
            }
            perror("Error sending SIGTERM");
            exit(EXIT_FAILURE);
        }
        printf("Sent SIGTERM to keydoggerd (pid %d)\n", pid);

        struct sigaction cancel_action;
        memset(&cancel_action, 0, sizeof(cancel_action));
        cancel_action.sa_handler = sigint_handler;
        sigemptyset(&cancel_action.sa_mask);
        sigaction(SIGINT, &cancel_action, NULL);

        if (pid_exit_wait(pid, SHUTDOWN_TIMEOUT_MS))
        {
            printf("keydoggerd stopped (pid %d)\n", pid);
            exit(EXIT_SUCCESS);
        }

        if (cancel_wait)
        {
            printf("Stopped waiting; keydoggerd (pid %d) will exit in the background\n", pid);
            exit(EXIT_SUCCESS);
        }

        fprintf(stderr, "keydoggerd still running after %d seconds\n", SHUTDOWN_TIMEOUT_MS / 1000);
        exit(EXIT_FAILURE);
    }
    else if (strcmp(argv[1], "restart") == 0)
    {
        pidfile_path_init();

        pid_t pid = pidfile_read();
        if (pid > 0)
        {
            if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
            {
                perror("Error sending SIGTERM");
                exit(EXIT_FAILURE);
            }
            printf("Sent SIGTERM to keydoggerd (pid %d)\n", pid);
        }

        if (pidfile_acquire_wait(SHUTDOWN_TIMEOUT_MS) < 0)
        {
            fprintf(stderr, "Timed out waiting for keydoggerd to stop\n");
            exit(EXIT_FAILURE);
        }

        read_keyboard_env();
        rc_load(NULL);
        daemonize_keydogger();
    }
    else if (strcmp(argv[1], "debug") == 0)
    {
        pidfile_path_init();

        pid_t pid = pidfile_read();
        if (pid > 0)
        {
            if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
            {
                perror("Error sending SIGTERM");
                exit(EXIT_FAILURE);
            }
            printf("Sent SIGTERM to keydoggerd (pid %d)\n", pid);
            if (pidfile_acquire_wait(SHUTDOWN_TIMEOUT_MS) < 0)
            {
                fprintf(stderr, "Timed out waiting for keydoggerd to stop\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            int pidfile_status = pidfile_acquire(NULL);
            if (pidfile_status == 1)
            {
                printf("Already running\n");
                exit(EXIT_SUCCESS);
            }
            if (pidfile_status < 0)
            {
                perror("Error opening pidfile");
                exit(EOPEN);
            }
        }

        prctl(PR_SET_NAME, DAEMON);

        read_keyboard_env();
        rc_load(DEBUG_RC_PATH);
        keydogger_daemon();
    }
    else if (strcmp(argv[1], "viz") == 0)
    {
        rc_load(DEBUG_RC_PATH);
        print_trie(TRIE, 0);
    }
    else
    {
        fprintf(stderr, "Usage error: keydogger start | stop | status | debug | viz\n");
        exit(EUSAGE);
    }
}
