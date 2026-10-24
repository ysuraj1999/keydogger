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
#include <ctype.h>
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
#define CONFIG_RELOAD_DEBOUNCE_MS 100

#define MAX_KEYBOARDS 16

#define CONFIG_WATCH_MASK (IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF)

static char *EvdevKeyboards[MAX_KEYBOARDS];
static struct pollfd KeyboardPollfds[MAX_KEYBOARDS];
static size_t KeyboardCount = 0;
static int LiveKeyboards = 0;
static int ActiveKeyboard = -1;

static bool IsShifted = false;
static struct trie *CurrentTrie = NULL;

static char *ConfigPath = NULL;
static int InotifyFd = -1;
static int WatchDescriptor = -1;
static bool WatchLost = false;
static bool ReloadPending = false;
static int64_t ReloadDeadlineMs = 0;

static char *DAEMON = "keydoggerd";
static struct trie *TRIE = NULL;

char *DEBUG_RC_PATH = "./keydoggerrc";

static int VKeyboardDevice = -1;
static int ReadyFd = -1;
static int PidfileFd = -1;
static char PidfilePath[PATH_MAX];
static volatile sig_atomic_t CancelWait = 0;

// Direct mapping of char codes to linux key codes
static int CharCodesToKeyCodes[] = {
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

static int KeyCodesToPosition[MAX_KEY_CODE];

void init_key_to_position_map(void)
{
    memset(KeyCodesToPosition, -1, sizeof(KeyCodesToPosition));
    KeyCodesToPosition[KEY_SPACE] = 31;
    KeyCodesToPosition[KEY_1 | FLAG_UPPERCASE] = 32;
    KeyCodesToPosition[KEY_APOSTROPHE | FLAG_UPPERCASE] = 33;
    KeyCodesToPosition[KEY_3 | FLAG_UPPERCASE] = 34;
    KeyCodesToPosition[KEY_4 | FLAG_UPPERCASE] = 35;
    KeyCodesToPosition[KEY_5 | FLAG_UPPERCASE] = 36;
    KeyCodesToPosition[KEY_7 | FLAG_UPPERCASE] = 37;
    KeyCodesToPosition[KEY_APOSTROPHE] = 38;
    KeyCodesToPosition[KEY_9 | FLAG_UPPERCASE] = 39;
    KeyCodesToPosition[KEY_0 | FLAG_UPPERCASE] = 40;
    KeyCodesToPosition[KEY_8 | FLAG_UPPERCASE] = 41;
    KeyCodesToPosition[KEY_EQUAL | FLAG_UPPERCASE] = 42;
    KeyCodesToPosition[KEY_COMMA] = 43;
    KeyCodesToPosition[KEY_MINUS] = 44;
    KeyCodesToPosition[KEY_DOT] = 45;
    KeyCodesToPosition[KEY_SLASH] = 46;
    KeyCodesToPosition[KEY_0] = 47;
    KeyCodesToPosition[KEY_1] = 48;
    KeyCodesToPosition[KEY_2] = 49;
    KeyCodesToPosition[KEY_3] = 50;
    KeyCodesToPosition[KEY_4] = 51;
    KeyCodesToPosition[KEY_5] = 52;
    KeyCodesToPosition[KEY_6] = 53;
    KeyCodesToPosition[KEY_7] = 54;
    KeyCodesToPosition[KEY_8] = 55;
    KeyCodesToPosition[KEY_9] = 56;
    KeyCodesToPosition[KEY_SEMICOLON | FLAG_UPPERCASE] = 57;
    KeyCodesToPosition[KEY_SEMICOLON] = 58;
    KeyCodesToPosition[KEY_COMMA | FLAG_UPPERCASE] = 59;
    KeyCodesToPosition[KEY_EQUAL] = 60;
    KeyCodesToPosition[KEY_DOT | FLAG_UPPERCASE] = 61;
    KeyCodesToPosition[KEY_SLASH | FLAG_UPPERCASE] = 62;
    KeyCodesToPosition[KEY_2 | FLAG_UPPERCASE] = 63;
    KeyCodesToPosition[KEY_A | FLAG_UPPERCASE] = 64;
    KeyCodesToPosition[KEY_B | FLAG_UPPERCASE] = 65;
    KeyCodesToPosition[KEY_C | FLAG_UPPERCASE] = 66;
    KeyCodesToPosition[KEY_D | FLAG_UPPERCASE] = 67;
    KeyCodesToPosition[KEY_E | FLAG_UPPERCASE] = 68;
    KeyCodesToPosition[KEY_F | FLAG_UPPERCASE] = 69;
    KeyCodesToPosition[KEY_G | FLAG_UPPERCASE] = 70;
    KeyCodesToPosition[KEY_H | FLAG_UPPERCASE] = 71;
    KeyCodesToPosition[KEY_I | FLAG_UPPERCASE] = 72;
    KeyCodesToPosition[KEY_J | FLAG_UPPERCASE] = 73;
    KeyCodesToPosition[KEY_K | FLAG_UPPERCASE] = 74;
    KeyCodesToPosition[KEY_L | FLAG_UPPERCASE] = 75;
    KeyCodesToPosition[KEY_M | FLAG_UPPERCASE] = 76;
    KeyCodesToPosition[KEY_N | FLAG_UPPERCASE] = 77;
    KeyCodesToPosition[KEY_O | FLAG_UPPERCASE] = 78;
    KeyCodesToPosition[KEY_P | FLAG_UPPERCASE] = 79;
    KeyCodesToPosition[KEY_Q | FLAG_UPPERCASE] = 80;
    KeyCodesToPosition[KEY_R | FLAG_UPPERCASE] = 81;
    KeyCodesToPosition[KEY_S | FLAG_UPPERCASE] = 82;
    KeyCodesToPosition[KEY_T | FLAG_UPPERCASE] = 83;
    KeyCodesToPosition[KEY_U | FLAG_UPPERCASE] = 84;
    KeyCodesToPosition[KEY_V | FLAG_UPPERCASE] = 85;
    KeyCodesToPosition[KEY_W | FLAG_UPPERCASE] = 86;
    KeyCodesToPosition[KEY_X | FLAG_UPPERCASE] = 87;
    KeyCodesToPosition[KEY_Y | FLAG_UPPERCASE] = 88;
    KeyCodesToPosition[KEY_Z | FLAG_UPPERCASE] = 89;
    KeyCodesToPosition[KEY_LEFTBRACE] = 90;
    KeyCodesToPosition[KEY_BACKSLASH] = 91;
    KeyCodesToPosition[KEY_RIGHTBRACE] = 92;
    KeyCodesToPosition[KEY_6 | FLAG_UPPERCASE] = 93;
    KeyCodesToPosition[KEY_MINUS | FLAG_UPPERCASE] = 94;
    KeyCodesToPosition[KEY_GRAVE] = 95;
    KeyCodesToPosition[KEY_A] = 96;
    KeyCodesToPosition[KEY_B] = 97;
    KeyCodesToPosition[KEY_C] = 98;
    KeyCodesToPosition[KEY_D] = 99;
    KeyCodesToPosition[KEY_E] = 100;
    KeyCodesToPosition[KEY_F] = 101;
    KeyCodesToPosition[KEY_G] = 102;
    KeyCodesToPosition[KEY_H] = 103;
    KeyCodesToPosition[KEY_I] = 104;
    KeyCodesToPosition[KEY_J] = 105;
    KeyCodesToPosition[KEY_K] = 106;
    KeyCodesToPosition[KEY_L] = 107;
    KeyCodesToPosition[KEY_M] = 108;
    KeyCodesToPosition[KEY_N] = 109;
    KeyCodesToPosition[KEY_O] = 110;
    KeyCodesToPosition[KEY_P] = 111;
    KeyCodesToPosition[KEY_Q] = 112;
    KeyCodesToPosition[KEY_R] = 113;
    KeyCodesToPosition[KEY_S] = 114;
    KeyCodesToPosition[KEY_T] = 115;
    KeyCodesToPosition[KEY_U] = 116;
    KeyCodesToPosition[KEY_V] = 117;
    KeyCodesToPosition[KEY_W] = 118;
    KeyCodesToPosition[KEY_X] = 119;
    KeyCodesToPosition[KEY_Y] = 120;
    KeyCodesToPosition[KEY_Z] = 121;
    KeyCodesToPosition[KEY_LEFTBRACE | FLAG_UPPERCASE] = 122;
    KeyCodesToPosition[KEY_BACKSLASH | FLAG_UPPERCASE] = 123;
    KeyCodesToPosition[KEY_RIGHTBRACE | FLAG_UPPERCASE] = 124;
    KeyCodesToPosition[KEY_GRAVE | FLAG_UPPERCASE] = 125;
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

bool pid_is_daemon(pid_t pid)
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

void pidfile_path_init(void)
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

    int length = snprintf(PidfilePath, sizeof(PidfilePath),
                    "%s/keydogger.pid", runtime_dir);
    if (length < 0 || (size_t)length >= sizeof(PidfilePath))
    {
        fprintf(stderr, "XDG_RUNTIME_DIR is too long\n");
        exit(EXIT_FAILURE);
    }
}

pid_t pidfile_read(void)
{
    int fd = open(PidfilePath, O_RDONLY | O_CLOEXEC);
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
int pidfile_acquire(pid_t *running_pid)
{
    int fd = open(PidfilePath, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
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

    PidfileFd = fd;
    return 0;
}

int pidfile_acquire_wait(int timeout_ms)
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

void pidfile_write(void)
{
    if (PidfileFd < 0)
    {
        return;
    }
    if (ftruncate(PidfileFd, 0) == 0)
    {
        lseek(PidfileFd, 0, SEEK_SET);
        dprintf(PidfileFd, "%d\n", getpid());
    }
}

void pidfile_release(void)
{
    if (PidfileFd < 0)
    {
        return;
    }
    close(PidfileFd);
    PidfileFd = -1;
}

void sigint_handler(int sig)
{
    (void)sig;
    CancelWait = 1;
}

bool pid_exit_wait(pid_t pid, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 50)
    {
        if (kill(pid, 0) < 0 && errno == ESRCH)
        {
            return true;
        }
        if (CancelWait)
        {
            return false;
        }
        usleep(50 * 1000);
    }
    return kill(pid, 0) < 0 && errno == ESRCH;
}

void cleanup(void)
{
    if (VKeyboardDevice >= 0)
    {
        ioctl(VKeyboardDevice, UI_DEV_DESTROY);
        close(VKeyboardDevice);
        VKeyboardDevice = -1;
    }

    for (size_t i = 0; i < KeyboardCount; i++)
    {
        if (KeyboardPollfds[i].fd >= 0)
        {
            close(KeyboardPollfds[i].fd);
            KeyboardPollfds[i].fd = -1;
        }
        free(EvdevKeyboards[i]);
        EvdevKeyboards[i] = NULL;
    }
    KeyboardCount = 0;
    LiveKeyboards = 0;
    ActiveKeyboard = -1;
    CurrentTrie = NULL;
    IsShifted = false;

    if (InotifyFd >= 0)
    {
        close(InotifyFd);
        InotifyFd = -1;
    }
    WatchDescriptor = -1;
    WatchLost = false;
    ReloadPending = false;

    free(ConfigPath);
    ConfigPath = NULL;

    trie_cleanup(TRIE);
    TRIE = NULL;

    pidfile_release();
}

bool try_send_key_to_device(int fd, struct input_event event)
{
    ssize_t written;
    do
    {
        written = write(fd, &event, sizeof(event));
    } while (written < 0 && errno == EINTR);

    return written == (ssize_t)sizeof(event);
}

bool try_send_sync(int fd)
{
    struct input_event event = {0};
    event.type = EV_SYN;
    event.value = SYN_REPORT;
    return try_send_key_to_device(fd, event);
}

bool is_printable_ascii(char *string)
{
    for (size_t i = 0; string[i] != '\0'; i++)
    {
        unsigned char character = (unsigned char)string[i];
        if (character < 32 || character > 126)
            return false;
    }
    return true;
}

struct trie *rc_parse(const char *path)
{
    FILE *rc_file = fopen(path, "r");
    if (rc_file == NULL)
    {
        fprintf(stderr, "Error opening %s\n", path);
        return NULL;
    }

    struct trie *root = malloc(sizeof(struct trie));
    if (root == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        fclose(rc_file);
        return NULL;
    }
    trie_init(root, NULL);

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
        trie_push(root, key, value);
    }

    bool read_error = ferror(rc_file);
    free(line);
    fclose(rc_file);

    if (read_error)
    {
        fprintf(stderr, "Error reading %s\n", path);
        trie_cleanup(root);
        return NULL;
    }

    return root;
}

void rc_load(const char *path)
{
    char rc_file_path[PATH_MAX];

    if (path != NULL)
    {
        int path_length = snprintf(rc_file_path, sizeof(rc_file_path), "%s", path);
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

        int path_length = snprintf(rc_file_path, sizeof(rc_file_path), "%s/%s", home, RC_PATH);
        if (path_length < 0 || (size_t)path_length >= sizeof(rc_file_path))
        {
            fprintf(stderr, "Error constructing config path %s\n", RC_PATH);
            exit(ESTR);
        }
    }

    char absolute_path[PATH_MAX];
    if (rc_file_path[0] != '/')
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("getcwd");
            exit(ESTR);
        }
        int path_length = snprintf(absolute_path, sizeof(absolute_path), "%s/%s", cwd, rc_file_path);
        if (path_length < 0 || (size_t)path_length >= sizeof(absolute_path))
        {
            fprintf(stderr, "Config path too long\n");
            exit(ESTR);
        }
    }
    else
    {
        int path_length = snprintf(absolute_path, sizeof(absolute_path), "%s", rc_file_path);
        if (path_length < 0 || (size_t)path_length >= sizeof(absolute_path))
        {
            fprintf(stderr, "Config path too long\n");
            exit(ESTR);
        }
    }

    free(ConfigPath);
    ConfigPath = strdup(absolute_path);
    if (ConfigPath == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        exit(ESTR);
    }

    TRIE = rc_parse(ConfigPath);
    if (TRIE == NULL)
    {
        exit(EOPEN);
    }
}

bool rc_reload(void)
{
    if (ConfigPath == NULL)
    {
        return false;
    }

    struct trie *new_trie = rc_parse(ConfigPath);
    if (new_trie == NULL)
    {
        exit(EREAD);
    }

    struct trie *old_trie = TRIE;
    TRIE = new_trie;
    CurrentTrie = new_trie;
    trie_cleanup(old_trie);
    fprintf(stderr, "Reloaded %s\n", ConfigPath);
    return true;
}

int64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
        perror("clock_gettime");
        exit(EREAD);
    }
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool config_watch_init(void)
{
    if (ConfigPath == NULL)
    {
        return false;
    }

    InotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (InotifyFd < 0)
    {
        perror("inotify_init1");
        return false;
    }

    WatchDescriptor = inotify_add_watch(InotifyFd, ConfigPath, CONFIG_WATCH_MASK);
    if (WatchDescriptor < 0)
    {
        perror("inotify_add_watch");
        close(InotifyFd);
        InotifyFd = -1;
        return false;
    }

    WatchLost = false;
    return true;
}

void config_watch_drain(void)
{
    if (InotifyFd < 0)
    {
        return;
    }

    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t length;
    while ((length = read(InotifyFd, buffer, sizeof(buffer))) > 0)
    {
        for (char *ptr = buffer; ptr < buffer + length;)
        {
            const struct inotify_event *event = (const struct inotify_event *)ptr;
            bool schedule_reload = true;

            if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF))
            {
                if (WatchDescriptor >= 0)
                {
                    inotify_rm_watch(InotifyFd, WatchDescriptor);
                    WatchDescriptor = -1;
                }
                WatchLost = true;
            }
            else if (event->mask & IN_IGNORED)
            {
                if (event->wd == WatchDescriptor)
                {
                    WatchDescriptor = -1;
                    WatchLost = true;
                }
                else
                {
                    schedule_reload = false;
                }
            }

            if (schedule_reload)
            {
                ReloadDeadlineMs = now_ms() + CONFIG_RELOAD_DEBOUNCE_MS;
                ReloadPending = true;
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    if (length < 0 && errno != EAGAIN && errno != EINTR)
    {
        fprintf(stderr, "Error reading inotify: %s\n", strerror(errno));
        exit(EREAD);
    }
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
    int keycode = CharCodesToKeyCodes[position];
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
            send_shift_down(VKeyboardDevice);
        send_key_to_device(VKeyboardDevice, event);
        usleep(SLEEP_TIME);
        event.value = 0;
        send_key_to_device(VKeyboardDevice, event);
        usleep(SLEEP_TIME);
        if (key.is_shifted)
            send_shift_up(VKeyboardDevice);
    }
    send_sync(VKeyboardDevice);
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

void trie_push(struct trie *root, char *key, char *expansion)
{
    struct trie *node = root;
    for (size_t i = 0; i < strlen(key); i++)
    {
        char character = key[i];
        struct key key_data = get_key_from_char(character);
        int position = key_data.position;
        if (node->next[position] == NULL)
        {
            struct trie *child = malloc(sizeof(struct trie));
            if (child == NULL)
            {
                fprintf(stderr, "Out of memory\n");
                exit(ESTR);
            }
            trie_init(child, &key_data);
            child->size = node->size + 1;
            child->parent = node;
            node->next[position] = child;
        }
        node = node->next[position];
    }
    node->is_leaf = true;
    free(node->expansion);
    node->expansion = malloc(strlen(expansion) + 1);
    if (node->expansion == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        exit(ESTR);
    }
    strcpy(node->expansion, expansion);
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

void signal_cleanup_handler(int sig)
{
    (void)sig;
    cleanup();
    exit(0);
}

void keyboard_disconnect(size_t index)
{
    int fd = KeyboardPollfds[index].fd;
    if (fd < 0)
    {
        return;
    }

    close(fd);
    KeyboardPollfds[index].fd = -1;
    KeyboardPollfds[index].events = 0;
    KeyboardPollfds[index].revents = 0;
    LiveKeyboards--;

    fprintf(stderr, "Warning: keyboard %s disconnected\n", EvdevKeyboards[index]);

    if (fd == ActiveKeyboard)
    {
        ActiveKeyboard = -1;
        IsShifted = false;
        CurrentTrie = TRIE;
    }

    if (LiveKeyboards <= 0)
    {
        fprintf(stderr, "No keyboards left; exiting\n");
        exit(EREAD);
    }
}

void keydogger_daemon(void)
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

    for (size_t i = 0; i < KeyboardCount; i++)
    {
        int fd = open(EvdevKeyboards[i], O_RDWR);
        if (fd < 0)
        {
            fprintf(stderr, "Warning: cannot open keyboard %s: %s\n",
                    EvdevKeyboards[i], strerror(errno));
            KeyboardPollfds[i].fd = -1;
            continue;
        }
        KeyboardPollfds[i].fd = fd;
        KeyboardPollfds[i].events = POLLIN;
        KeyboardPollfds[i].revents = 0;
        LiveKeyboards++;
    }

    if (LiveKeyboards == 0)
    {
        fprintf(stderr, "No keyboards could be opened\n");
        exit(EOPEN);
    }

    VKeyboardDevice = open(UINPUT_PATH, O_WRONLY);
    if (VKeyboardDevice < 0)
    {
        fprintf(stderr, "Error opening %s\n", UINPUT_PATH);
        exit(EOPEN);
    }

    init_virtual_device(VKeyboardDevice);

    init_key_to_position_map();

    if (!config_watch_init())
    {
        fprintf(stderr, "Warning: auto-reload disabled\n");
    }

    pidfile_write();

    // Let the parent process know that startup succeeded
    if (ReadyFd >= 0)
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
            write(ReadyFd, ready_message, ready_size);
        }
        close(ReadyFd);
        ReadyFd = -1;
    }

    IsShifted = false;
    CurrentTrie = TRIE;
    ActiveKeyboard = -1;

    while (1)
    {
        struct pollfd PollFds[MAX_KEYBOARDS + 1];
        size_t PollCount = KeyboardCount;
        for (size_t i = 0; i < KeyboardCount; i++)
        {
            PollFds[i] = KeyboardPollfds[i];
        }
        if (InotifyFd >= 0)
        {
            PollFds[PollCount].fd = InotifyFd;
            PollFds[PollCount].events = POLLIN;
            PollFds[PollCount].revents = 0;
            PollCount++;
        }

        int timeout = -1;
        if (ReloadPending)
        {
            int64_t now = now_ms();
            int64_t remaining = ReloadDeadlineMs - now;
            timeout = remaining > 0 ? (int)remaining : 0;
        }

        int ready = poll(PollFds, PollCount, timeout);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "Error polling keyboards: %s\n", strerror(errno));
            exit(EREAD);
        }

        for (size_t i = 0; i < KeyboardCount; i++)
        {
            int fd = KeyboardPollfds[i].fd;
            if (fd < 0)
            {
                continue;
            }

            if (PollFds[i].revents & (POLLHUP | POLLERR | POLLNVAL))
            {
                keyboard_disconnect(i);
                continue;
            }
            if (!(PollFds[i].revents & POLLIN))
            {
                continue;
            }

            struct input_event event;
            ssize_t read_inputs = read(fd, &event, sizeof(event));
            if (read_inputs != (ssize_t)sizeof(event))
            {
                if (read_inputs < 0 && (errno == EINTR || errno == EAGAIN))
                {
                    continue;
                }
                keyboard_disconnect(i);
                continue;
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
            // ignore autorepeat
            if (event.value == 2)
            {
                continue;
            }

            if (event.value == 0)
            {
                if (fd != ActiveKeyboard)
                {
                    continue;
                }
                if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)
                {
                    IsShifted = false;
                }
                continue;
            }

            if (event.value != 1)
            {
                continue;
            }

            if (fd != ActiveKeyboard)
            {
                ActiveKeyboard = fd;
                IsShifted = false;
                CurrentTrie = TRIE;
            }

            // Handle shift down
            if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)
            {
                IsShifted = true;
                continue;
            }

            int event_code;
            if (IsShifted)
                event_code = event.code | FLAG_UPPERCASE;
            else
                event_code = event.code;
            int position = KeyCodesToPosition[event_code];

            // if next doesnt match trigger, reset
            if (CurrentTrie->next[position] == NULL)
            {
                CurrentTrie = TRIE;
                continue;
            }

            struct trie *next = CurrentTrie->next[position];
            // if next doesnt match trigger, reset
            if ((event_code ^ CharCodesToKeyCodes[position]) != 0)
            {
                CurrentTrie = TRIE;
                continue;
            }
            // if next is terminal, expand it
            if (next->is_leaf)
            {
                struct trie *next_copy = next;
                int trigger_fd = ActiveKeyboard;
                bool physical_ok = trigger_fd >= 0;

                // RELEASE ALL TRIGGER KEYS
                event.value = 0;
                // release last key
                if (physical_ok &&
                    (!try_send_key_to_device(trigger_fd, event) ||
                     !try_send_sync(trigger_fd)))
                {
                    physical_ok = false;
                }

                if (physical_ok)
                {
                    event.code = KEY_LEFTSHIFT;
                    if (!try_send_key_to_device(trigger_fd, event) ||
                        !try_send_sync(trigger_fd))
                    {
                        physical_ok = false;
                    }
                }

                if (physical_ok)
                {
                    event.code = KEY_RIGHTSHIFT;
                    if (!try_send_key_to_device(trigger_fd, event) ||
                        !try_send_sync(trigger_fd))
                    {
                        physical_ok = false;
                    }
                }

                while (physical_ok && next_copy->parent != NULL &&
                       next_copy->parent != TRIE)
                {
                    event.code = next_copy->parent->keycode;
                    if (!try_send_key_to_device(trigger_fd, event) ||
                        !try_send_sync(trigger_fd))
                    {
                        physical_ok = false;
                        break;
                    }
                    next_copy = next_copy->parent;
                }

                if (!physical_ok)
                {
                    keyboard_disconnect(i);
                }

                // EXPAND
                send_backspace(VKeyboardDevice, next->size);
                send_to_keyboard(next->expansion);

                // Reset trie head
                CurrentTrie = TRIE;
            }
            else
            {
                CurrentTrie = next;
            }
        }

        if (InotifyFd >= 0 && (PollFds[KeyboardCount].revents & POLLIN))
        {
            config_watch_drain();
        }

        if (ReloadPending && now_ms() >= ReloadDeadlineMs)
        {
            ReloadPending = false;

            if (WatchLost)
            {
                WatchDescriptor = inotify_add_watch(InotifyFd, ConfigPath, CONFIG_WATCH_MASK);
                if (WatchDescriptor < 0)
                {
                    perror(ConfigPath);
                    exit(EREAD);
                }
                WatchLost = false;
            }

            rc_reload();
        }
    }
}

void daemonize_keydogger(void)
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
    ReadyFd = ready_pipe[1];

    if (prctl(PR_SET_NAME, DAEMON) < 0)
    {
        fprintf(stderr, "Error setting name for process\n");
        exit(ERENAM);
    }

    // Close inherited descriptors, keeping the readiness pipe
    long max_fd = sysconf(_SC_OPEN_MAX);
    for (long open_fd = max_fd - 1; open_fd > STDERR_FILENO; open_fd--)
    {
        if (open_fd != ReadyFd && open_fd != PidfileFd)
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

char *trim_whitespace(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text))
    {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
    {
        end--;
    }
    *end = '\0';
    return text;
}

void read_keyboard_env(void)
{
#if DEBUG_MODE == 1
    const char *spec = KEYDOGGER_KEYBOARD;
#else
    const char *spec = getenv("KEYDOGGER_KEYBOARD");
#endif

    if (spec == NULL || spec[0] == '\0')
    {
        fprintf(stderr, "Required environment variable missing - KEYDOGGER_KEYBOARD\n");
        exit(EENV);
    }

    char *copy = strdup(spec);
    if (copy == NULL)
    {
        fprintf(stderr, "Out of memory parsing KEYDOGGER_KEYBOARD\n");
        exit(EENV);
    }

    KeyboardCount = 0;
    char *save = NULL;
    for (char *token = strtok_r(copy, ",", &save); token != NULL;
         token = strtok_r(NULL, ",", &save))
    {
        char *path = trim_whitespace(token);
        if (path[0] == '\0')
        {
            continue;
        }

        bool duplicate = false;
        for (size_t i = 0; i < KeyboardCount; i++)
        {
            if (strcmp(EvdevKeyboards[i], path) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        if (KeyboardCount >= MAX_KEYBOARDS)
        {
            fprintf(stderr, "Too many keyboards in KEYDOGGER_KEYBOARD (max %d)\n", MAX_KEYBOARDS);
            free(copy);
            exit(EENV);
        }

        EvdevKeyboards[KeyboardCount] = strdup(path);
        if (EvdevKeyboards[KeyboardCount] == NULL)
        {
            fprintf(stderr, "Out of memory parsing KEYDOGGER_KEYBOARD\n");
            free(copy);
            exit(EENV);
        }
        KeyboardPollfds[KeyboardCount].fd = -1;
        KeyboardPollfds[KeyboardCount].events = 0;
        KeyboardPollfds[KeyboardCount].revents = 0;
        KeyboardCount++;
    }

    free(copy);

    if (KeyboardCount == 0)
    {
        fprintf(stderr, "KEYDOGGER_KEYBOARD must contain at least one keyboard\n");
        exit(EENV);
    }
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

        if (CancelWait)
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
