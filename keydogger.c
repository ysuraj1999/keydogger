#include <stdio.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>

#include "keydogger.h"

#define READABLE_KEYS 126

#define STARTUP_TIMEOUT_MS 5000
#define SHUTDOWN_TIMEOUT_MS 5000
#define CONFIG_RELOAD_DEBOUNCE_MS 100

#define CONFIG_WATCH_MASK (IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF)

static const char *DaemonName = "keydoggerd";
static const char *DebugRcPath = "./keydoggerrc";

static struct keydogger Keydogger;
static volatile sig_atomic_t StopCancel = 0;

/* --- keymap data --- */

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

static const size_t KeyCodes[] = {
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

/* --- keymap --- */

void keymap_create(void)
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

bool keymap_code_is_supported(size_t code)
{
    if (code >= KEY_1 && code <= KEY_EQUAL)
        return true;
    if (code >= KEY_Q && code <= KEY_RIGHTBRACE)
        return true;
    if (code >= KEY_A && code <= KEY_LEFTSHIFT)
        return true;
    if (code >= KEY_BACKSLASH && code <= KEY_RIGHTSHIFT)
        return true;
    if (code == KEY_SPACE)
        return true;
    return false;
}

struct key keymap_key_from_char(char character)
{
    struct key key = {0};
    key.character = character;
    key.is_shifted = false;
    int position = (int)character - 1;
    int keycode = CharCodesToKeyCodes[position];
    if (keycode & FLAG_UPPERCASE)
    {
        key.is_shifted = true;
        keycode &= ~FLAG_UPPERCASE;
    }
    key.keycode = keycode;
    key.position = position;
    return key;
}

/* --- text helpers --- */

bool text_is_printable_ascii(const char *string)
{
    for (size_t i = 0; string[i] != '\0'; i++)
    {
        unsigned char character = (unsigned char)string[i];
        if (character < 32 || character > 126)
            return false;
    }
    return true;
}

char *text_trim_whitespace(char *text)
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

/* --- trie --- */

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

static struct trie *trie_node_create(struct trie *parent, const struct key *key)
{
    struct trie *node = malloc(sizeof(*node));
    if (node == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        return NULL;
    }

    node->character = '\0';
    node->is_shifted = false;
    node->parent = parent;
    node->is_leaf = false;
    node->expansion = NULL;
    node->size = parent != NULL ? parent->size + 1 : 0;
    if (key != NULL)
    {
        node->character = key->character;
        node->keycode = key->keycode;
        node->is_shifted = key->is_shifted;
    }
    for (size_t i = 0; i < READABLE_KEYS; i++)
    {
        node->next[i] = NULL;
    }
    return node;
}

struct trie *trie_create(void)
{
    return trie_node_create(NULL, NULL);
}

void trie_destroy(struct trie *trie)
{
    if (trie == NULL)
    {
        return;
    }

    free(trie->expansion);
    for (size_t i = 0; i < READABLE_KEYS; i++)
    {
        trie_destroy(trie->next[i]);
    }
    free(trie);
}

void trie_insert(struct trie *root, const char *key, const char *expansion)
{
    struct trie *node = root;
    for (size_t i = 0; i < strlen(key); i++)
    {
        struct key key_data = keymap_key_from_char(key[i]);
        int position = key_data.position;
        if (node->next[position] == NULL)
        {
            struct trie *child = trie_node_create(node, &key_data);
            if (child == NULL)
            {
                exit(ESTR);
            }
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

void trie_print(struct trie *trie, size_t level)
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
        trie_print(trie->next[i], level + 1);
    }
}

/* --- config --- */

struct trie *config_parse(const char *path)
{
    FILE *rc_file = fopen(path, "r");
    if (rc_file == NULL)
    {
        fprintf(stderr, "Error opening %s\n", path);
        return NULL;
    }

    struct trie *root = trie_create();
    if (root == NULL)
    {
        fclose(rc_file);
        return NULL;
    }

    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t line_length;
    while ((line_length = getline(&line, &line_capacity, rc_file)) != -1)
    {
        while (line_length > 0 &&
               (line[line_length - 1] == '\n' || line[line_length - 1] == '\r'))
        {
            line[--line_length] = '\0';
        }

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

        if (!text_is_printable_ascii(key) || !text_is_printable_ascii(value))
        {
            fprintf(stderr, "Warning: skipping non-ASCII expansion for trigger %s\n", key);
            continue;
        }
        trie_insert(root, key, value);
    }

    bool read_error = ferror(rc_file);
    free(line);
    fclose(rc_file);

    if (read_error)
    {
        fprintf(stderr, "Error reading %s\n", path);
        trie_destroy(root);
        return NULL;
    }

    return root;
}

bool config_load(struct config *config, const char *path)
{
    char rc_file_path[PATH_MAX];

    if (path != NULL)
    {
        int path_length = snprintf(rc_file_path, sizeof(rc_file_path), "%s", path);
        if (path_length < 0 || (size_t)path_length >= sizeof(rc_file_path))
        {
            fprintf(stderr, "Config path too long\n");
            return false;
        }
    }
    else
    {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0')
        {
            fprintf(stderr, "Error determining home directory\n");
            return false;
        }

        int path_length = snprintf(rc_file_path, sizeof(rc_file_path), "%s/%s", home, RC_PATH);
        if (path_length < 0 || (size_t)path_length >= sizeof(rc_file_path))
        {
            fprintf(stderr, "Error constructing config path %s\n", RC_PATH);
            return false;
        }
    }

    char absolute_path[PATH_MAX];
    if (rc_file_path[0] != '/')
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("getcwd");
            return false;
        }
        int path_length = snprintf(absolute_path, sizeof(absolute_path), "%s/%s", cwd, rc_file_path);
        if (path_length < 0 || (size_t)path_length >= sizeof(absolute_path))
        {
            fprintf(stderr, "Config path too long\n");
            return false;
        }
    }
    else
    {
        int path_length = snprintf(absolute_path, sizeof(absolute_path), "%s", rc_file_path);
        if (path_length < 0 || (size_t)path_length >= sizeof(absolute_path))
        {
            fprintf(stderr, "Config path too long\n");
            return false;
        }
    }

    free(config->path);
    config->path = strdup(absolute_path);
    if (config->path == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        return false;
    }

    struct trie *trie = config_parse(config->path);
    if (trie == NULL)
    {
        return false;
    }
    config->trie = trie;
    return true;
}

bool config_reload(struct config *config)
{
    if (config->path == NULL)
    {
        return false;
    }

    struct trie *new_trie = config_parse(config->path);
    if (new_trie == NULL)
    {
        return false;
    }

    struct trie *old_trie = config->trie;
    config->trie = new_trie;
    trie_destroy(old_trie);
    return true;
}

void config_unload(struct config *config)
{
    trie_destroy(config->trie);
    config->trie = NULL;
    free(config->path);
    config->path = NULL;
}

bool config_is_loaded(const struct config *config)
{
    return config->trie != NULL;
}

/* --- config watch --- */

int64_t monotonic_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
        perror("clock_gettime");
        exit(EREAD);
    }
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool config_watch_open(struct config_watch *watch, const char *path)
{
    if (path == NULL)
    {
        return false;
    }

    watch->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch->inotify_fd < 0)
    {
        perror("inotify_init1");
        return false;
    }

    watch->watch_fd = inotify_add_watch(watch->inotify_fd, path, CONFIG_WATCH_MASK);
    if (watch->watch_fd < 0)
    {
        perror("inotify_add_watch");
        close(watch->inotify_fd);
        watch->inotify_fd = -1;
        return false;
    }

    watch->lost = false;
    watch->reload_pending = false;
    watch->reload_deadline_ms = 0;
    return true;
}

bool config_watch_rearm(struct config_watch *watch, const char *path)
{
    if (watch->inotify_fd < 0 || path == NULL)
    {
        return false;
    }

    watch->watch_fd = inotify_add_watch(watch->inotify_fd, path, CONFIG_WATCH_MASK);
    if (watch->watch_fd < 0)
    {
        return false;
    }

    watch->lost = false;
    return true;
}

void config_watch_drain(struct config_watch *watch)
{
    if (watch->inotify_fd < 0)
    {
        return;
    }

    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t length;
    while ((length = read(watch->inotify_fd, buffer, sizeof(buffer))) > 0)
    {
        for (char *ptr = buffer; ptr < buffer + length;)
        {
            const struct inotify_event *event = (const struct inotify_event *)ptr;
            bool schedule_reload = true;

            if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF))
            {
                if (watch->watch_fd >= 0)
                {
                    inotify_rm_watch(watch->inotify_fd, watch->watch_fd);
                    watch->watch_fd = -1;
                }
                watch->lost = true;
            }
            else if (event->mask & IN_IGNORED)
            {
                if (event->wd == watch->watch_fd)
                {
                    watch->watch_fd = -1;
                    watch->lost = true;
                }
                else
                {
                    schedule_reload = false;
                }
            }

            if (schedule_reload)
            {
                watch->reload_deadline_ms = monotonic_now_ms() + CONFIG_RELOAD_DEBOUNCE_MS;
                watch->reload_pending = true;
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

bool config_watch_reload_is_due(const struct config_watch *watch)
{
    return watch->reload_pending &&
           monotonic_now_ms() >= watch->reload_deadline_ms;
}

bool config_watch_is_lost(const struct config_watch *watch)
{
    return watch->lost;
}

void config_watch_close(struct config_watch *watch)
{
    if (watch->inotify_fd >= 0)
    {
        close(watch->inotify_fd);
        watch->inotify_fd = -1;
    }
    watch->watch_fd = -1;
    watch->lost = false;
    watch->reload_pending = false;
    watch->reload_deadline_ms = 0;
}

/* --- pidfile --- */

bool pidfile_process_is_daemon(pid_t pid)
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
    return strcmp(comm, DaemonName) == 0;
}

void pidfile_create(struct pidfile *pidfile)
{
    pidfile->fd = -1;
    pidfile->path[0] = '\0';

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

    int length = snprintf(pidfile->path, sizeof(pidfile->path),
                          "%s/keydogger.pid", runtime_dir);
    if (length < 0 || (size_t)length >= sizeof(pidfile->path))
    {
        fprintf(stderr, "XDG_RUNTIME_DIR is too long\n");
        exit(EXIT_FAILURE);
    }
}

int pidfile_acquire(struct pidfile *pidfile, pid_t *running_pid)
{
    int fd = open(pidfile->path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
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

    pidfile->fd = fd;
    return 0;
}

int pidfile_acquire_wait(struct pidfile *pidfile, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 100)
    {
        int status = pidfile_acquire(pidfile, NULL);
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

void pidfile_write(struct pidfile *pidfile)
{
    if (pidfile->fd < 0)
    {
        return;
    }
    if (ftruncate(pidfile->fd, 0) == 0)
    {
        lseek(pidfile->fd, 0, SEEK_SET);
        dprintf(pidfile->fd, "%d\n", getpid());
    }
}

pid_t pidfile_read(struct pidfile *pidfile)
{
    int fd = open(pidfile->path, O_RDONLY | O_CLOEXEC);
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
    if (pidfile_process_is_daemon(pid))
    {
        return pid;
    }
    return -1;
}

void pidfile_release(struct pidfile *pidfile)
{
    if (pidfile->fd < 0)
    {
        return;
    }
    close(pidfile->fd);
    pidfile->fd = -1;
}

void pidfile_destroy(struct pidfile *pidfile)
{
    pidfile_release(pidfile);
    pidfile->path[0] = '\0';
}

bool pidfile_is_locked(const struct pidfile *pidfile)
{
    return pidfile->fd >= 0;
}

bool process_wait_exit(pid_t pid, int timeout_ms, const volatile sig_atomic_t *cancel)
{
    for (int waited = 0; waited < timeout_ms; waited += 50)
    {
        if (kill(pid, 0) < 0 && errno == ESRCH)
        {
            return true;
        }
        if (cancel != NULL && *cancel)
        {
            return false;
        }
        usleep(50 * 1000);
    }
    return kill(pid, 0) < 0 && errno == ESRCH;
}

/* --- keyboards --- */

void keyboards_configure(struct keyboard_set *keyboards)
{
    for (size_t i = 0; i < keyboards->count; i++)
    {
        if (keyboards->items[i].fd >= 0)
        {
            close(keyboards->items[i].fd);
        }
        free(keyboards->items[i].path);
        keyboards->items[i].fd = -1;
        keyboards->items[i].path = NULL;
    }
    keyboards->count = 0;
    keyboards->live = 0;
    keyboards->configured = false;

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

    char *save = NULL;
    for (char *token = strtok_r(copy, ",", &save); token != NULL;
         token = strtok_r(NULL, ",", &save))
    {
        char *path = text_trim_whitespace(token);
        if (path[0] == '\0')
        {
            continue;
        }

        bool duplicate = false;
        for (size_t i = 0; i < keyboards->count; i++)
        {
            if (strcmp(keyboards->items[i].path, path) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        if (keyboards->count >= MAX_KEYBOARDS)
        {
            fprintf(stderr, "Too many keyboards in KEYDOGGER_KEYBOARD (max %d)\n", MAX_KEYBOARDS);
            free(copy);
            exit(EENV);
        }

        keyboards->items[keyboards->count].path = strdup(path);
        if (keyboards->items[keyboards->count].path == NULL)
        {
            fprintf(stderr, "Out of memory parsing KEYDOGGER_KEYBOARD\n");
            free(copy);
            exit(EENV);
        }
        keyboards->items[keyboards->count].fd = -1;
        keyboards->count++;
    }

    free(copy);

    if (keyboards->count == 0)
    {
        fprintf(stderr, "KEYDOGGER_KEYBOARD must contain at least one keyboard\n");
        exit(EENV);
    }

    keyboards->configured = true;
}

bool keyboard_open(struct keyboard *keyboard)
{
    keyboard->fd = open(keyboard->path, O_RDWR);
    return keyboard->fd >= 0;
}

void keyboard_close(struct keyboard *keyboard)
{
    if (keyboard->fd >= 0)
    {
        close(keyboard->fd);
        keyboard->fd = -1;
    }
}

bool keyboards_open(struct keyboard_set *keyboards)
{
    if (!keyboards->configured)
    {
        return false;
    }

    keyboards->live = 0;
    for (size_t i = 0; i < keyboards->count; i++)
    {
        if (keyboard_open(&keyboards->items[i]))
        {
            keyboards->live++;
        }
        else
        {
            fprintf(stderr, "Warning: cannot open keyboard %s: %s\n",
                    keyboards->items[i].path, strerror(errno));
        }
    }

    return keyboards->live > 0;
}

void keyboards_close(struct keyboard_set *keyboards)
{
    for (size_t i = 0; i < keyboards->count; i++)
    {
        keyboard_close(&keyboards->items[i]);
    }
    keyboards->live = 0;
}

void keyboards_destroy(struct keyboard_set *keyboards)
{
    keyboards_close(keyboards);
    for (size_t i = 0; i < keyboards->count; i++)
    {
        free(keyboards->items[i].path);
        keyboards->items[i].path = NULL;
        keyboards->items[i].fd = -1;
    }
    keyboards->count = 0;
    keyboards->configured = false;
}

void keyboards_disconnect(struct keyboard_set *keyboards, size_t index)
{
    int fd = keyboards->items[index].fd;
    if (fd < 0)
    {
        return;
    }

    close(fd);
    keyboards->items[index].fd = -1;
    keyboards->live--;

    fprintf(stderr, "Warning: keyboard %s disconnected\n", keyboards->items[index].path);

    if (keyboards->live <= 0)
    {
        fprintf(stderr, "No keyboards left; exiting\n");
        exit(EREAD);
    }
}

bool keyboards_are_configured(const struct keyboard_set *keyboards)
{
    return keyboards->configured;
}

bool keyboards_are_open(const struct keyboard_set *keyboards)
{
    return keyboards->live > 0;
}

bool keyboard_read_event(struct keyboard *keyboard, struct input_event *event)
{
    ssize_t read_inputs = read(keyboard->fd, event, sizeof(*event));
    return read_inputs == (ssize_t)sizeof(*event);
}

/* --- trigger --- */

void trigger_create(struct trigger *trigger)
{
    trigger->active_fd = -1;
    trigger->shifted = false;
    trigger->cursor = NULL;
}

void trigger_reset(struct trigger *trigger)
{
    trigger->active_fd = -1;
    trigger->shifted = false;
    trigger->cursor = NULL;
}

void trigger_cancel(struct trigger *trigger)
{
    trigger->cursor = NULL;
}

void trigger_switch_keyboard(struct trigger *trigger, int fd)
{
    trigger->active_fd = fd;
    trigger->shifted = false;
    trigger->cursor = NULL;
}

struct trie *trigger_feed(struct trigger *trigger, struct trie *root, int fd,
                          struct input_event event)
{
    if (root == NULL)
    {
        return NULL;
    }
    if (event.type != EV_KEY)
    {
        return NULL;
    }
    if (!keymap_code_is_supported(event.code))
    {
        return NULL;
    }
    if (event.value == 2)
    {
        return NULL;
    }

    if (event.value == 0)
    {
        if (fd != trigger->active_fd)
        {
            return NULL;
        }
        if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)
        {
            trigger->shifted = false;
        }
        return NULL;
    }

    if (event.value != 1)
    {
        return NULL;
    }

    if (fd != trigger->active_fd)
    {
        trigger_switch_keyboard(trigger, fd);
    }

    if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)
    {
        trigger->shifted = true;
        return NULL;
    }

    int event_code = trigger->shifted ? (event.code | FLAG_UPPERCASE) : event.code;
    if (event_code < 0 || event_code >= MAX_KEY_CODE)
    {
        trigger->cursor = NULL;
        return NULL;
    }

    int position = KeyCodesToPosition[event_code];
    if (position < 0 || position >= READABLE_KEYS)
    {
        trigger->cursor = NULL;
        return NULL;
    }

    struct trie *node = trigger->cursor != NULL ? trigger->cursor : root;
    if (node->next[position] == NULL)
    {
        trigger->cursor = NULL;
        return NULL;
    }

    struct trie *next = node->next[position];
    if ((event_code ^ CharCodesToKeyCodes[position]) != 0)
    {
        trigger->cursor = NULL;
        return NULL;
    }

    if (next->is_leaf)
    {
        trigger->cursor = NULL;
        return next;
    }

    trigger->cursor = next;
    return NULL;
}

bool trigger_is_matching(const struct trigger *trigger)
{
    return trigger->cursor != NULL;
}

bool trigger_has_active_keyboard(const struct trigger *trigger)
{
    return trigger->active_fd >= 0;
}

/* --- virtual keyboard --- */

bool vkeyboard_open(struct vkeyboard *vkeyboard)
{
    vkeyboard->fd = open(UINPUT_PATH, O_WRONLY);
    vkeyboard->created = false;
    return vkeyboard->fd >= 0;
}

bool vkeyboard_create(struct vkeyboard *vkeyboard)
{
    struct uinput_setup usetup = {0};
    int status;

    if ((status = ioctl(vkeyboard->fd, UI_SET_EVBIT, EV_KEY)) < 0)
    {
        fprintf(stderr, "Error initializing virtual input\n");
        exit(EINIT);
    }
    if ((status = ioctl(vkeyboard->fd, UI_SET_KEYBIT, KEY_BACKSPACE)) < 0)
    {
        fprintf(stderr, "Error adding key to virtual input : %d\n", KEY_BACKSPACE);
        exit(EADD);
    }
    if ((status = ioctl(vkeyboard->fd, UI_SET_KEYBIT, KEY_LEFTCTRL)) < 0)
    {
        fprintf(stderr, "Error adding key to virtual input : %d\n", KEY_LEFTCTRL);
        exit(EADD);
    }
    for (size_t i = 0; i < LINUX_KEYS; i++)
    {
        if ((status = ioctl(vkeyboard->fd, UI_SET_KEYBIT, KeyCodes[i])) < 0)
        {
            fprintf(stderr, "Error adding key to virtual input : %ld\n", KeyCodes[i]);
            exit(EADD);
        }
    }

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 1187;
    usetup.id.product = 1999;
    strcpy(usetup.name, "keydogger");

    if ((status = ioctl(vkeyboard->fd, UI_DEV_SETUP, &usetup)) < 0)
    {
        fprintf(stderr, "Error setting up virtual device\n");
        exit(ESETUP);
    }
    if ((status = ioctl(vkeyboard->fd, UI_DEV_CREATE)) < 0)
    {
        fprintf(stderr, "Error creating up virtual device\n");
        exit(ECREATE);
    }

    vkeyboard->created = true;
    return true;
}

void vkeyboard_destroy(struct vkeyboard *vkeyboard)
{
    if (vkeyboard->created && vkeyboard->fd >= 0)
    {
        ioctl(vkeyboard->fd, UI_DEV_DESTROY);
    }
    vkeyboard->created = false;
}

void vkeyboard_close(struct vkeyboard *vkeyboard)
{
    if (vkeyboard->fd >= 0)
    {
        close(vkeyboard->fd);
        vkeyboard->fd = -1;
    }
    vkeyboard->created = false;
}

bool vkeyboard_is_created(const struct vkeyboard *vkeyboard)
{
    return vkeyboard->created;
}

void vkeyboard_emit_text(struct vkeyboard *vkeyboard, const char *string)
{
    struct input_event event = {0};
    event.type = EV_KEY;
    for (size_t i = 0; i < strlen(string); i++)
    {
        struct key key = keymap_key_from_char(string[i]);
        event.code = key.keycode;
        event.value = 1;
        if (key.is_shifted)
        {
            vkeyboard_emit_shift_down(vkeyboard);
        }
        input_write_event(vkeyboard->fd, event);
        usleep(SLEEP_TIME);
        event.value = 0;
        input_write_event(vkeyboard->fd, event);
        usleep(SLEEP_TIME);
        if (key.is_shifted)
        {
            vkeyboard_emit_shift_up(vkeyboard);
        }
    }
    input_write_sync(vkeyboard->fd);
}

void vkeyboard_emit_backspace(struct vkeyboard *vkeyboard, size_t n)
{
    struct input_event event = {0};
    event.code = KEY_BACKSPACE;
    event.type = EV_KEY;
    for (size_t i = 0; i < n; i++)
    {
        event.value = 1;
        input_write_event(vkeyboard->fd, event);
        event.value = 0;
        input_write_event(vkeyboard->fd, event);
        input_write_sync(vkeyboard->fd);
    }
}

void vkeyboard_emit_shift_down(struct vkeyboard *vkeyboard)
{
    struct input_event event = {0};
    event.type = EV_KEY;
    event.code = KEY_LEFTSHIFT;
    event.value = 1;
    input_write_event(vkeyboard->fd, event);
    usleep(SLEEP_TIME);
}

void vkeyboard_emit_shift_up(struct vkeyboard *vkeyboard)
{
    struct input_event event = {0};
    event.type = EV_KEY;
    event.code = KEY_LEFTSHIFT;
    event.value = 0;
    input_write_event(vkeyboard->fd, event);
    usleep(SLEEP_TIME);
}

/* --- low-level evdev writes --- */

bool input_try_write_event(int fd, struct input_event event)
{
    ssize_t written;
    do
    {
        written = write(fd, &event, sizeof(event));
    } while (written < 0 && errno == EINTR);

    return written == (ssize_t)sizeof(event);
}

bool input_try_write_sync(int fd)
{
    struct input_event event = {0};
    event.type = EV_SYN;
    event.value = SYN_REPORT;
    return input_try_write_event(fd, event);
}

void input_write_event(int fd, struct input_event event)
{
    ssize_t written;
    do
    {
        written = write(fd, &event, sizeof(event));
    } while (written < 0 && errno == EINTR);

    if (written != (ssize_t)sizeof(event))
    {
        fprintf(stderr, "Error writing to virtual device\n");
        exit(EWRITE);
    }
}

void input_write_sync(int fd)
{
    struct input_event event = {0};
    event.type = EV_SYN;
    event.value = SYN_REPORT;
    input_write_event(fd, event);
}

/* --- daemon --- */

void keydogger_create(struct keydogger *keydogger)
{
    memset(keydogger, 0, sizeof(*keydogger));

    keydogger->config.path = NULL;
    keydogger->config.trie = NULL;

    keydogger->watch.inotify_fd = -1;
    keydogger->watch.watch_fd = -1;

    keydogger->pidfile.fd = -1;
    keydogger->pidfile.path[0] = '\0';

    for (size_t i = 0; i < MAX_KEYBOARDS; i++)
    {
        keydogger->keyboards.items[i].fd = -1;
        keydogger->keyboards.items[i].path = NULL;
    }

    trigger_create(&keydogger->trigger);

    keydogger->vkeyboard.fd = -1;
    keydogger->vkeyboard.created = false;
}

void daemon_install_signal_handlers(void)
{
    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signals_handle_termination;
    sigfillset(&sa.sa_mask);

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
}

void signals_handle_termination(int sig)
{
    (void)sig;
    daemon_cleanup(&Keydogger);
    exit(0);
}

void daemon_reload_config(struct keydogger *keydogger)
{
    trigger_cancel(&keydogger->trigger);
    if (!config_reload(&keydogger->config))
    {
        fprintf(stderr, "Reload failed\n");
        exit(EREAD);
    }
    fprintf(stderr, "Reloaded %s\n", keydogger->config.path);
}

void daemon_disconnect_keyboard(struct keydogger *keydogger, size_t index)
{
    int fd = keydogger->keyboards.items[index].fd;
    if (fd >= 0 && fd == keydogger->trigger.active_fd)
    {
        trigger_reset(&keydogger->trigger);
    }
    keyboards_disconnect(&keydogger->keyboards, index);
}

void daemon_expand(struct keydogger *keydogger, size_t keyboard_index,
                   struct trie *leaf, struct input_event event)
{
    int trigger_fd = keydogger->keyboards.items[keyboard_index].fd;
    struct trie *next_copy = leaf;
    bool physical_ok = trigger_fd >= 0;

    event.value = 0;
    if (physical_ok &&
        (!input_try_write_event(trigger_fd, event) ||
         !input_try_write_sync(trigger_fd)))
    {
        physical_ok = false;
    }

    if (physical_ok)
    {
        event.code = KEY_LEFTSHIFT;
        if (!input_try_write_event(trigger_fd, event) ||
            !input_try_write_sync(trigger_fd))
        {
            physical_ok = false;
        }
    }

    if (physical_ok)
    {
        event.code = KEY_RIGHTSHIFT;
        if (!input_try_write_event(trigger_fd, event) ||
            !input_try_write_sync(trigger_fd))
        {
            physical_ok = false;
        }
    }

    while (physical_ok && next_copy->parent != NULL &&
           next_copy->parent != keydogger->config.trie)
    {
        event.code = next_copy->parent->keycode;
        if (!input_try_write_event(trigger_fd, event) ||
            !input_try_write_sync(trigger_fd))
        {
            physical_ok = false;
            break;
        }
        next_copy = next_copy->parent;
    }

    if (!physical_ok)
    {
        daemon_disconnect_keyboard(keydogger, keyboard_index);
    }

    vkeyboard_emit_backspace(&keydogger->vkeyboard, leaf->size);
    vkeyboard_emit_text(&keydogger->vkeyboard, leaf->expansion);
}

void daemon_cleanup(struct keydogger *keydogger)
{
    vkeyboard_destroy(&keydogger->vkeyboard);
    vkeyboard_close(&keydogger->vkeyboard);

    keyboards_close(&keydogger->keyboards);
    keyboards_destroy(&keydogger->keyboards);

    config_watch_close(&keydogger->watch);
    trigger_reset(&keydogger->trigger);
    config_unload(&keydogger->config);

    pidfile_destroy(&keydogger->pidfile);
}

void daemon_run(struct keydogger *keydogger, int ready_fd)
{
    daemon_install_signal_handlers();

    if (!keyboards_open(&keydogger->keyboards))
    {
        fprintf(stderr, "No keyboards could be opened\n");
        exit(EOPEN);
    }

    if (!vkeyboard_open(&keydogger->vkeyboard))
    {
        fprintf(stderr, "Error opening %s\n", UINPUT_PATH);
        exit(EOPEN);
    }
    vkeyboard_create(&keydogger->vkeyboard);

    keymap_create();
    trigger_create(&keydogger->trigger);

    if (!config_watch_open(&keydogger->watch, keydogger->config.path))
    {
        fprintf(stderr, "Warning: auto-reload disabled\n");
    }

    pidfile_write(&keydogger->pidfile);

    if (ready_fd >= 0)
    {
        char message[64];
        int length = snprintf(message, sizeof(message),
                              "keydoggerd started at pid %d\n", getpid());
        if (length > 0)
        {
            size_t size = (size_t)length;
            if (size >= sizeof(message))
            {
                size = sizeof(message) - 1;
            }
            write(ready_fd, message, size);
        }
        close(ready_fd);
    }

    while (1)
    {
        struct pollfd PollFds[MAX_KEYBOARDS + 1];
        size_t PollCount = keydogger->keyboards.count;

        for (size_t i = 0; i < keydogger->keyboards.count; i++)
        {
            PollFds[i].fd = keydogger->keyboards.items[i].fd;
            PollFds[i].events = POLLIN;
            PollFds[i].revents = 0;
        }

        if (keydogger->watch.inotify_fd >= 0)
        {
            PollFds[PollCount].fd = keydogger->watch.inotify_fd;
            PollFds[PollCount].events = POLLIN;
            PollFds[PollCount].revents = 0;
            PollCount++;
        }

        int timeout = -1;
        if (keydogger->watch.reload_pending)
        {
            int64_t remaining = keydogger->watch.reload_deadline_ms - monotonic_now_ms();
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

        for (size_t i = 0; i < keydogger->keyboards.count; i++)
        {
            int fd = keydogger->keyboards.items[i].fd;
            if (fd < 0)
            {
                continue;
            }

            if (PollFds[i].revents & (POLLHUP | POLLERR | POLLNVAL))
            {
                daemon_disconnect_keyboard(keydogger, i);
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
                daemon_disconnect_keyboard(keydogger, i);
                continue;
            }

            struct trie *leaf = trigger_feed(&keydogger->trigger,
                                            keydogger->config.trie, fd, event);
            if (leaf != NULL)
            {
                daemon_expand(keydogger, i, leaf, event);
            }
        }

        if (keydogger->watch.inotify_fd >= 0 &&
            (PollFds[keydogger->keyboards.count].revents & POLLIN))
        {
            config_watch_drain(&keydogger->watch);
        }

        if (config_watch_reload_is_due(&keydogger->watch))
        {
            keydogger->watch.reload_pending = false;
            if (config_watch_is_lost(&keydogger->watch))
            {
                if (!config_watch_rearm(&keydogger->watch, keydogger->config.path))
                {
                    perror("inotify_add_watch");
                    exit(EREAD);
                }
            }
            daemon_reload_config(keydogger);
        }
    }
}

void daemonize(struct keydogger *keydogger)
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

    if (pid > 0)
    {
        _exit(EXIT_SUCCESS);
    }

    int ready_fd = ready_pipe[1];

    if (prctl(PR_SET_NAME, DaemonName) < 0)
    {
        fprintf(stderr, "Error setting name for process\n");
        exit(ERENAM);
    }

    long max_fd = sysconf(_SC_OPEN_MAX);
    for (long open_fd = max_fd - 1; open_fd > STDERR_FILENO; open_fd--)
    {
        if (open_fd != ready_fd && open_fd != keydogger->pidfile.fd)
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

    daemon_run(keydogger, ready_fd);
}

/* --- cli --- */

void cli_usage(void)
{
    fprintf(stderr, "Usage error: keydogger start | stop | status | debug | viz\n");
}

void cli_stop_handle_sigint(int sig)
{
    (void)sig;
    StopCancel = 1;
}

void cli_start(struct keydogger *keydogger)
{
    pidfile_create(&keydogger->pidfile);

    pid_t running_pid = 0;
    int pidfile_status = pidfile_acquire(&keydogger->pidfile, &running_pid);
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

    keyboards_configure(&keydogger->keyboards);
    if (!config_load(&keydogger->config, NULL))
    {
        exit(EOPEN);
    }
    daemonize(keydogger);
}

void cli_status(struct keydogger *keydogger)
{
    pidfile_create(&keydogger->pidfile);

    pid_t pid = pidfile_read(&keydogger->pidfile);
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

void cli_stop(struct keydogger *keydogger)
{
    pidfile_create(&keydogger->pidfile);

    pid_t pid = pidfile_read(&keydogger->pidfile);
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
    cancel_action.sa_handler = cli_stop_handle_sigint;
    sigemptyset(&cancel_action.sa_mask);
    sigaction(SIGINT, &cancel_action, NULL);

    if (process_wait_exit(pid, SHUTDOWN_TIMEOUT_MS, &StopCancel))
    {
        printf("keydoggerd stopped (pid %d)\n", pid);
        exit(EXIT_SUCCESS);
    }

    if (StopCancel)
    {
        printf("Stopped waiting; keydoggerd (pid %d) will exit in the background\n", pid);
        exit(EXIT_SUCCESS);
    }

    fprintf(stderr, "keydoggerd still running after %d seconds\n", SHUTDOWN_TIMEOUT_MS / 1000);
    exit(EXIT_FAILURE);
}

void cli_restart(struct keydogger *keydogger)
{
    pidfile_create(&keydogger->pidfile);

    pid_t pid = pidfile_read(&keydogger->pidfile);
    if (pid > 0)
    {
        if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
        {
            perror("Error sending SIGTERM");
            exit(EXIT_FAILURE);
        }
        printf("Sent SIGTERM to keydoggerd (pid %d)\n", pid);
    }

    if (pidfile_acquire_wait(&keydogger->pidfile, SHUTDOWN_TIMEOUT_MS) < 0)
    {
        fprintf(stderr, "Timed out waiting for keydoggerd to stop\n");
        exit(EXIT_FAILURE);
    }

    keyboards_configure(&keydogger->keyboards);
    if (!config_load(&keydogger->config, NULL))
    {
        exit(EOPEN);
    }
    daemonize(keydogger);
}

void cli_debug(struct keydogger *keydogger)
{
    pidfile_create(&keydogger->pidfile);

    pid_t pid = pidfile_read(&keydogger->pidfile);
    if (pid > 0)
    {
        if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
        {
            perror("Error sending SIGTERM");
            exit(EXIT_FAILURE);
        }
        printf("Sent SIGTERM to keydoggerd (pid %d)\n", pid);
        if (pidfile_acquire_wait(&keydogger->pidfile, SHUTDOWN_TIMEOUT_MS) < 0)
        {
            fprintf(stderr, "Timed out waiting for keydoggerd to stop\n");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        int pidfile_status = pidfile_acquire(&keydogger->pidfile, NULL);
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

    prctl(PR_SET_NAME, DaemonName);

    keyboards_configure(&keydogger->keyboards);
    if (!config_load(&keydogger->config, DebugRcPath))
    {
        exit(EOPEN);
    }
    daemon_run(keydogger, -1);
}

void cli_viz(struct keydogger *keydogger)
{
    (void)keydogger;

    struct config config = {0};
    if (!config_load(&config, DebugRcPath))
    {
        exit(EOPEN);
    }
    trie_print(config.trie, 0);
    config_unload(&config);
}

int main(int argc, char *argv[])
{
    keydogger_create(&Keydogger);

    if (argc < 2)
    {
        cli_usage();
        exit(EUSAGE);
    }

    if (strcmp(argv[1], "start") == 0)
    {
        cli_start(&Keydogger);
    }
    else if (strcmp(argv[1], "status") == 0)
    {
        cli_status(&Keydogger);
    }
    else if (strcmp(argv[1], "stop") == 0)
    {
        cli_stop(&Keydogger);
    }
    else if (strcmp(argv[1], "restart") == 0)
    {
        cli_restart(&Keydogger);
    }
    else if (strcmp(argv[1], "debug") == 0)
    {
        cli_debug(&Keydogger);
    }
    else if (strcmp(argv[1], "viz") == 0)
    {
        cli_viz(&Keydogger);
    }
    else
    {
        cli_usage();
        exit(EUSAGE);
    }

    return 0;
}
