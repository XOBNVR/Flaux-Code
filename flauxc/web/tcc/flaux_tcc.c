/* TinyCC for Flaux Studio on the web (see build_tcc.py): run it with its arguments as one text, one per line. */
#include <string.h>
int tcc_main(int argc, char **argv);
int flaux_tcc(char *args) {
    char *argv[128];
    int argc = 0;
    argv[argc++] = "tcc";
    for (char *p = strtok(args, "\n"); p && argc < 127; p = strtok(NULL, "\n")) argv[argc++] = p;
    argv[argc] = 0;
    return tcc_main(argc, argv);
}
