#include <lib.h>

void touch(char *path) {
    int r;
    char temp[128];
    strcpy(temp, path);
    r = create(temp, FTYPE_REG);
    if (r < 0 && r != -E_FILE_EXISTS) {
        printf("touch: cannot touch '<file>': No such file or directory\n");
    }
}

int main(int argc, char **argv) {
    touch(argv[1]);
	return 0;
}