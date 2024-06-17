#include <lib.h>

int flag[256];

void rm(char *path) {
    int r;
    char temp[128];
    strcpy(temp, path);
    if (flag['r']) {
        r = remove(temp, 0);
        if (r == -E_NOT_FOUND) {
            printf("rm: cannot remove '%s': No such file or directory\n", path);
        }
        return;
    }
    else if (flag['f']) {
        r = remove(temp, 0);
        return;
    }
    else {
        r = remove(temp, 1);
        if (r == -E_NOT_FOUND) {
            printf("rm: cannot remove '%s': No such file or directory\n", path);
        }
        if (r == -E_NOT_REMOVE) {
            printf("rm: cannot remove '%s': Is a directory\n", path);
        }
    }
   
}

int main(int argc, char **argv) {

    char **o_argv = argv;

    if (strcmp("-r", argv[1]) == 0) {
        flag['r']++;
    } else if (strcmp("-rf", argv[1]) == 0) {
        flag['f']++;
    }

    if (argc > 2) {
        rm(o_argv[2]);
    } else {
        rm(o_argv[1]);
    }
    //rm(argv[1]);
    return 0;
}