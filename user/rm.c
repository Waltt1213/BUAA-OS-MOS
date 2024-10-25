#include <lib.h>

int flag[256];

int rm(char *path) {
    int r;
    char temp[128];
    strcpy(temp, path);
    if (flag['r']) {
        r = remove(temp, 0);
        if (r == -E_NOT_FOUND) {
            printf("rm: cannot remove '%s': No such file or directory\n", path);
            return -1;
        }
        return 0;
    }
    else if (flag['f']) {
        r = remove(temp, 0);
        return 0;
    }
    else {
        r = remove(temp, 1);
        if (r == -E_NOT_FOUND) {
            printf("rm: cannot remove '%s': No such file or directory\n", path);
            return -1;
        }
        if (r == -E_NOT_REMOVE) {
            printf("rm: cannot remove '%s': Is a directory\n", path);
            return -1;
        }
        return 0;
    }
}

int main(int argc, char **argv) {

    char **o_argv = argv;
    int r;
    if (strcmp("-r", argv[1]) == 0) {
        flag['r']++;
    } else if (strcmp("-rf", argv[1]) == 0) {
        flag['f']++;
    }

    if (argc > 2) {
        r = rm(o_argv[2]);
    } else {
        r = rm(o_argv[1]);
    }
    //rm(argv[1]);
    if (r < 0) return -1;
    return 0;
}