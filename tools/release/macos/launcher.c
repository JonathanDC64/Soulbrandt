/* Written for Demon's Souls. The Mac app's own program (docs/178 section 1): the file macOS starts when the player
 * opens the app. It runs the Windows program in the Wine that the app carries.
 *
 * The app (tools/release/macos_app.py) holds, in Contents/Resources:
 *   wine/     a macOS build of Wine (bin/wine, its libraries, MoltenVK);
 *   program/  the Windows download's files, as the release packs them (setup/release-info.txt names the exe).
 *
 * At each start:
 *   1. The data folder: ~/Library/Application Support/<the fixed folder name>, with Wine's own folder (its prefix:
 *      the pretend C: drive, where the game keeps its settings, saves and setup's build) in wine/.
 *   2. The program is copied out of the app into program/ there, when the app's is newer or there is none yet. It
 *      runs from that copy: setup gives the program its icon and an update replaces its files, as on Windows, and
 *      neither may change the app itself (that breaks its signature; and a downloaded app runs from a read-only copy
 *      until it is moved). A copy that an update made newer than the app's is kept.
 *   3. Wine starts the program. Wine makes its prefix itself on the first start (no .NET or browser parts: the game
 *      needs neither). Its messages go to wine.log, this program's to launcher.log, both in the data folder.
 *
 * Built for x86-64 only (macos_app.py's command): macOS then offers to install Rosetta, which Wine needs on Apple
 * silicon, when it is missing. A script in its place would get "Bad CPU type" instead.
 */
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

/* The data folder's name: setup_folder.hpp's kOneFolderName, so that the game's own folder and the app's are one. */
#define DATA_FOLDER "Soulbrandt"  /* (the fixed folder name) */
#ifndef DITTO
#define DITTO "/usr/bin/ditto"    /* (a test builds it with its own) */
#endif

static FILE* s_log;

static void say(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (s_log) {
        va_list cp;
        va_copy(cp, ap);
        vfprintf(s_log, fmt, cp);
        fputc('\n', s_log);
        fflush(s_log);
        va_end(cp);
    }
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int join(char* out, size_t n, const char* a, const char* b)
{
    const int k = snprintf(out, n, "%s/%s", a, b);
    return k > 0 && (size_t)k < n;
}

static int make_dirs(const char* path)
{
    char p[PATH_MAX];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return 0;
    for (char* s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, 0755) != 0 && errno != EEXIST) return 0;
        *s = '/';
    }
    return mkdir(p, 0755) == 0 || errno == EEXIST;
}

/* The value of `key=` in a release-info.txt (empty when there is none). */
static void info_value(const char* file, const char* key, char* out, size_t n)
{
    out[0] = 0;
    FILE* f = fopen(file, "r");
    if (!f) return;
    char line[1024];
    const size_t k = strlen(key);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, k) || line[k] != '=') continue;
        char* v = line + k + 1;
        v[strcspn(v, "\r\n")] = 0;
        snprintf(out, n, "%s", v);
        break;
    }
    fclose(f);
}

/* Compares two versions, X.Y.Z with an optional -pre part (a pre-release comes before its release): <0, 0, >0. */
static int version_cmp(const char* a, const char* b)
{
    unsigned long va[3] = {0, 0, 0}, vb[3] = {0, 0, 0};
    char* end;
    const char* pa = a;
    const char* pb = b;
    for (int i = 0; i < 3; i++) {
        va[i] = strtoul(pa, &end, 10);
        pa = (*end == '.') ? end + 1 : end;
        vb[i] = strtoul(pb, &end, 10);
        pb = (*end == '.') ? end + 1 : end;
    }
    for (int i = 0; i < 3; i++)
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    const char* ra = strchr(a, '-');
    const char* rb = strchr(b, '-');
    if (!ra && !rb) return 0;
    if (!ra) return 1;
    if (!rb) return -1;
    return strcmp(ra, rb);
}

/* Runs a program and waits for it: its exit status, or -1. */
static int run(char* const argv[])
{
    pid_t pid;
    const int e = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);
    if (e != 0) {
        say("could not start %s: %s", argv[0], strerror(e));
        return -1;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(int argc, char** argv)
{
    /* The app's Resources folder, from this program's own path (Contents/MacOS/<name>). */
    char exe[PATH_MAX], real[PATH_MAX], res[PATH_MAX];
    uint32_t size = sizeof exe;
    if (_NSGetExecutablePath(exe, &size) != 0 || !realpath(exe, real)) {
        fprintf(stderr, "the app's own path is not known\n");
        return 1;
    }
    char* slash = strrchr(real, '/');           /* .../Contents/MacOS/<name> */
    if (!slash) return 1;
    *slash = 0;
    slash = strrchr(real, '/');                 /* .../Contents/MacOS */
    if (!slash) return 1;
    *slash = 0;
    if (!join(res, sizeof res, real, "Resources")) return 1;

    const char* home = getenv("HOME");
    if (!home || !*home) {
        fprintf(stderr, "HOME is not set\n");
        return 1;
    }
    char root[PATH_MAX], prefix[PATH_MAX], program[PATH_MAX], path[PATH_MAX];
    if (snprintf(root, sizeof root, "%s/Library/Application Support/%s", home, DATA_FOLDER) >= (int)sizeof root ||
        !join(prefix, sizeof prefix, root, "wine") || !join(program, sizeof program, root, "program"))
        return 1;
    if (!make_dirs(root)) {
        fprintf(stderr, "could not make %s: %s\n", root, strerror(errno));
        return 1;
    }
    join(path, sizeof path, root, "launcher.log");
    s_log = fopen(path, "w");
    say("app: %s", real);

    /* The program in the app, and its copy in the data folder. */
    char app_info[PATH_MAX], copy_info[PATH_MAX], app_version[128], copy_version[128], exe_name[256];
    if (snprintf(app_info, sizeof app_info, "%s/program/setup/release-info.txt", res) >= (int)sizeof app_info ||
        snprintf(copy_info, sizeof copy_info, "%s/setup/release-info.txt", program) >= (int)sizeof copy_info)
        return 1;
    info_value(app_info, "version", app_version, sizeof app_version);
    info_value(app_info, "exe", exe_name, sizeof exe_name);
    info_value(copy_info, "version", copy_version, sizeof copy_version);
    if (!app_version[0] || !exe_name[0] || strchr(exe_name, '/')) {
        say("the app's program is incomplete: no version or exe in %s", app_info);
        return 1;
    }
    if (!copy_version[0] || version_cmp(app_version, copy_version) > 0) {
        /* ditto copies the folder's contents over the copy, keeping the files only the copy has (setup's). */
        say("program %s: copying it out of the app (the copy: %s)", app_version, copy_version[0] ? copy_version : "none");
        char from[PATH_MAX];
        join(from, sizeof from, res, "program");
        char* const cp[] = {DITTO, from, program, NULL};
        if (!make_dirs(program) || run(cp) != 0) {
            say("could not copy the program into %s", program);
            return 1;
        }
    } else {
        say("program %s (the app has %s)", copy_version, app_version);
    }

    /* Wine's environment. Values the player set already are kept. */
    setenv("WINEPREFIX", prefix, 1);
    setenv("WINEDLLOVERRIDES", "mscoree,mshtml=", 0);   /* no .NET or browser parts, and no question about them */
    setenv("WINEDEBUG", "fixme-all", 0);                /* Wine's errors only, into wine.log */
    setenv("MVK_CONFIG_FAST_MATH_ENABLED", "0", 0);     /* MoltenVK: the shaders' maths exact, as on a PC */
    setenv("DES_MAC_APP", "1", 1);                      /* the game: it runs in the Mac app */
    char wine_bin[PATH_MAX], new_path[PATH_MAX * 2];
    join(wine_bin, sizeof wine_bin, res, "wine/bin");
    const char* old_path = getenv("PATH");
    snprintf(new_path, sizeof new_path, "%s:%s", wine_bin, old_path && *old_path ? old_path : "/usr/bin:/bin");
    setenv("PATH", new_path, 1);

    /* Wine's messages into wine.log; then this process becomes Wine, running the program. */
    join(path, sizeof path, root, "wine.log");
    if (freopen(path, "w", stdout) == NULL || dup2(fileno(stdout), STDERR_FILENO) < 0)
        say("could not open %s; Wine's messages are not kept", path);
    char wine[PATH_MAX], target[PATH_MAX];
    join(wine, sizeof wine, wine_bin, "wine");
    join(target, sizeof target, program, exe_name);
    say("starting %s %s", wine, target);
    if (s_log) fclose(s_log);
    s_log = NULL;

    /* The program's arguments: the player's own, without the -psn_ one older macOS versions add. */
    char** args = calloc((size_t)argc + 3, sizeof *args);
    if (!args) return 1;
    int n = 0;
    args[n++] = wine;
    args[n++] = target;
    for (int i = 1; i < argc; i++)
        if (strncmp(argv[i], "-psn_", 5) != 0) args[n++] = argv[i];
    args[n] = NULL;
    execv(wine, args);
    fprintf(stderr, "could not start %s: %s\n", wine, strerror(errno));
    return 1;
}
