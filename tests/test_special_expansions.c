#define main keydogger_program_main
#include "../keydogger.c"
#undef main

#include <assert.h>

#define MAX_CAPTURED_EVENTS 8192

static int CaptureRead = -1;
static int CaptureWrite = -1;
static int Failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            Failures++;                                                         \
        }                                                                       \
    } while (0)

static char *write_config(const char *contents)
{
    char *path = strdup("/tmp/keydogger-test-XXXXXX");
    assert(path != NULL);

    int fd = mkstemp(path);
    assert(fd >= 0);

    size_t length = strlen(contents);
    if (length > 0)
    {
        assert(write(fd, contents, length) == (ssize_t)length);
    }
    close(fd);
    return path;
}

static void rewrite_config(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    fputs(contents, file);
    fclose(file);
}

static struct trie *find_leaf(struct trie *root, const char *trigger)
{
    struct trie *node = root;
    for (size_t i = 0; trigger[i] != '\0'; i++)
    {
        if (node == NULL)
        {
            return NULL;
        }
        struct key key = keymap_key_from_char(trigger[i]);
        if (key.position < 0)
        {
            return NULL;
        }
        node = node->next[key.position];
    }
    return node != NULL && node->is_leaf ? node : NULL;
}

static void capture_begin(void)
{
    int fds[2];
    assert(pipe(fds) == 0);

    int flags = fcntl(fds[0], F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == 0);

    CaptureRead = fds[0];
    CaptureWrite = fds[1];
}

static size_t capture_drain(struct input_event *events, size_t capacity)
{
    size_t total = 0;
    while (total < capacity)
    {
        ssize_t length = read(CaptureRead, &events[total],
                              (capacity - total) * sizeof(events[0]));
        if (length <= 0)
        {
            break;
        }
        total += (size_t)length / sizeof(events[0]);
    }
    return total;
}

static void capture_end(void)
{
    if (CaptureRead >= 0)
    {
        close(CaptureRead);
        CaptureRead = -1;
    }
    if (CaptureWrite >= 0)
    {
        close(CaptureWrite);
        CaptureWrite = -1;
    }
}

static char key_event_to_char(int keycode, bool shifted)
{
    for (unsigned char character = 32; character <= 126; character++)
    {
        struct key key = keymap_key_from_char((char)character);
        if (key.keycode == keycode && key.is_shifted == shifted)
        {
            return (char)character;
        }
    }
    return '\0';
}

static size_t events_to_text(const struct input_event *events, size_t count,
                             char *output, size_t capacity)
{
    size_t length = 0;
    bool shifted = false;

    for (size_t i = 0; i < count && length + 1 < capacity; i++)
    {
        const struct input_event *event = &events[i];
        if (event->type != EV_KEY)
        {
            continue;
        }

        if (event->code == KEY_LEFTSHIFT || event->code == KEY_RIGHTSHIFT)
        {
            shifted = event->value != 0;
            continue;
        }

        if (event->value == 0)
        {
            continue;
        }

        if (event->code == KEY_BACKSPACE)
        {
            if (length > 0)
            {
                length--;
            }
            continue;
        }

        char character = key_event_to_char(event->code, shifted);
        if (character != '\0')
        {
            output[length++] = character;
        }
    }

    output[length] = '\0';
    return length;
}

static size_t count_key_presses(const struct input_event *events, size_t count,
                                int keycode)
{
    size_t presses = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (events[i].type == EV_KEY && events[i].code == keycode &&
            events[i].value == 1)
        {
            presses++;
        }
    }
    return presses;
}

static void prepare_keydogger(struct keydogger *keydogger)
{
    keydogger_create(keydogger);
    keydogger->keyboards.count = 1;
    keydogger->keyboards.items[0].fd = -1;
    keydogger->vkeyboard.fd = -1;
}

static void expand(struct keydogger *keydogger, struct trie *leaf,
                   struct input_event *events, size_t *count, char *text,
                   size_t text_capacity)
{
    struct input_event event = {0};
    event.type = EV_KEY;
    event.code = KEY_S;
    event.value = 1;

    capture_begin();
    keydogger->vkeyboard.fd = CaptureWrite;
    daemon_expand(keydogger, 0, leaf, event);
    *count = capture_drain(events, MAX_CAPTURED_EVENTS);
    events_to_text(events, *count, text, text_capacity);
    capture_end();
}

static void test_normal_expansion(void)
{
    char *path = write_config(":;hi=Hello\n");
    struct keydogger keydogger;
    prepare_keydogger(&keydogger);

    CHECK(config_load(&keydogger.config, path));
    struct trie *leaf = find_leaf(keydogger.config.trie, ":;hi");
    CHECK(leaf != NULL);

    struct input_event events[MAX_CAPTURED_EVENTS];
    size_t count;
    char text[128];
    expand(&keydogger, leaf, events, &count, text, sizeof(text));

    CHECK(strcmp(text, "Hello") == 0);
    CHECK(count_key_presses(events, count, KEY_BACKSPACE) == leaf->size);
    CHECK(count_key_presses(events, count, KEY_H) == 1);
    CHECK(keydogger.enabled);

    config_unload(&keydogger.config);
    remove(path);
    free(path);
}

static void test_status_expansion(void)
{
    char *path = write_config(":;ks=KEYDOGGER_STATUS\n");
    struct keydogger keydogger;
    prepare_keydogger(&keydogger);

    CHECK(config_load(&keydogger.config, path));
    struct trie *leaf = find_leaf(keydogger.config.trie, ":;ks");
    CHECK(leaf != NULL);

    struct input_event events[MAX_CAPTURED_EVENTS];
    size_t count;
    char text[128];

    expand(&keydogger, leaf, events, &count, text, sizeof(text));
    CHECK(strcmp(text, KEYDOGGER_STATUS_ON) == 0);
    CHECK(count_key_presses(events, count, KEY_BACKSPACE) == leaf->size);
    CHECK(keydogger.enabled);

    keydogger.enabled = false;
    expand(&keydogger, leaf, events, &count, text, sizeof(text));
    CHECK(strcmp(text, KEYDOGGER_STATUS_OFF) == 0);
    CHECK(!keydogger.enabled);

    config_unload(&keydogger.config);
    remove(path);
    free(path);
}

static void test_toggle_expansion_and_disabled_expansions(void)
{
    char *path = write_config(":;kd=KEYDOGGER_TOGGLE\n:;hi=Hello\n");
    struct keydogger keydogger;
    prepare_keydogger(&keydogger);

    CHECK(config_load(&keydogger.config, path));
    struct trie *toggle = find_leaf(keydogger.config.trie, ":;kd");
    struct trie *normal = find_leaf(keydogger.config.trie, ":;hi");
    CHECK(toggle != NULL);
    CHECK(normal != NULL);

    struct input_event events[MAX_CAPTURED_EVENTS];
    size_t count;
    char text[128];

    expand(&keydogger, toggle, events, &count, text, sizeof(text));
    CHECK(strcmp(text, "") == 0);
    CHECK(count_key_presses(events, count, KEY_BACKSPACE) == toggle->size);
    CHECK(!keydogger.enabled);

    // Ordinary expansions are left alone while disabled.
    expand(&keydogger, normal, events, &count, text, sizeof(text));
    CHECK(count == 0);
    CHECK(!keydogger.enabled);

    // Toggle remains available while disabled and re-enables Keydogger.
    expand(&keydogger, toggle, events, &count, text, sizeof(text));
    CHECK(strcmp(text, "") == 0);
    CHECK(keydogger.enabled);

    // Ordinary expansions work again after re-enabling.
    expand(&keydogger, normal, events, &count, text, sizeof(text));
    CHECK(strcmp(text, "Hello") == 0);

    config_unload(&keydogger.config);
    remove(path);
    free(path);
}

static void test_failed_state_transitions(void)
{
    char *path = write_config(
        ":;lower=keydogger_toggle\n"
        ":;spaced=KEYDOGGER_STATUS \n");
    struct keydogger keydogger;
    prepare_keydogger(&keydogger);

    CHECK(config_load(&keydogger.config, path));
    struct trie *lower = find_leaf(keydogger.config.trie, ":;lower");
    struct trie *spaced = find_leaf(keydogger.config.trie, ":;spaced");
    CHECK(lower != NULL);
    CHECK(spaced != NULL);

    struct input_event events[MAX_CAPTURED_EVENTS];
    size_t count;
    char text[128];

    // Case variants are ordinary text, not actions.
    expand(&keydogger, lower, events, &count, text, sizeof(text));
    CHECK(strcmp(text, "keydogger_toggle") == 0);
    CHECK(keydogger.enabled);

    // Whitespace-padded values are ordinary text too.
    expand(&keydogger, spaced, events, &count, text, sizeof(text));
    CHECK(strcmp(text, "KEYDOGGER_STATUS ") == 0);
    CHECK(keydogger.enabled);

    config_unload(&keydogger.config);
    remove(path);
    free(path);
}

static void test_config_reload(void)
{
    char *path = write_config(":;ks=KEYDOGGER_STATUS\n:;old=Old\n");
    struct keydogger keydogger;
    prepare_keydogger(&keydogger);

    CHECK(config_load(&keydogger.config, path));
    CHECK(find_leaf(keydogger.config.trie, ":;old") != NULL);
    keydogger.enabled = false;

    struct config_watch watch = {0};
    watch.inotify_fd = -1;
    watch.watch_fd = -1;
    CHECK(config_watch_open(&watch, path));

    rewrite_config(path, ":;ks=KEYDOGGER_STATUS\n:;kd=KEYDOGGER_TOGGLE\n:;new=New\n");
    config_watch_drain(&watch);

    CHECK(watch.reload_pending);
    watch.reload_deadline_ms = monotonic_now_ms() - 1;
    CHECK(config_watch_reload_is_due(&watch));

    daemon_reload_config(&keydogger);

    CHECK(find_leaf(keydogger.config.trie, ":;old") == NULL);
    struct trie *status = find_leaf(keydogger.config.trie, ":;ks");
    struct trie *toggle = find_leaf(keydogger.config.trie, ":;kd");
    struct trie *normal = find_leaf(keydogger.config.trie, ":;new");
    CHECK(status != NULL);
    CHECK(toggle != NULL);
    CHECK(normal != NULL);
    CHECK(strcmp(status->expansion, KEYDOGGER_STATUS_EXPANSION) == 0);
    CHECK(strcmp(toggle->expansion, KEYDOGGER_TOGGLE_EXPANSION) == 0);
    CHECK(strcmp(normal->expansion, "New") == 0);
    CHECK(!keydogger.enabled);

    config_watch_close(&watch);
    config_unload(&keydogger.config);
    remove(path);
    free(path);
}

int main(void)
{
    keymap_create();

    test_normal_expansion();
    test_status_expansion();
    test_toggle_expansion_and_disabled_expansions();
    test_failed_state_transitions();
    test_config_reload();

    if (Failures != 0)
    {
        fprintf(stderr, "%d test(s) failed\n", Failures);
        return EXIT_FAILURE;
    }

    printf("All special expansion tests passed\n");
    return EXIT_SUCCESS;
}
