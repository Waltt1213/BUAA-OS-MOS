#include <lib.h>

int flag[256];

void mkdir(char *path) {
    int r;
    char temp[128];
    strcpy(temp, path);
    if (flag['p']) {
        r = create(temp, FTYPE_DIR);
        // 忽略错误，若目录已存在则退出，若父目录不存在则递归创建目录
        if (r == -E_FILE_EXISTS) {
            //user_panic("exit\n");
            return;
        }
        if (r == -E_NOT_FOUND) {
            // 递归创建父目录
            char *p = temp;
            if (*p == '/') {
                p++;
            }
            while (*p) {
                while (*p && *p != '/') p++; //找到下一个/，即一个目录
                *p = '\0';
                r = create(temp, FTYPE_DIR);
                *p = '/';
                p++;
            }
        }
    } else {
        r = create(temp, FTYPE_DIR);
        if (r == -E_FILE_EXISTS) {
            printf("mkdir: cannot create directory '<dir>': File exists\n");
            return;
        }
        if (r == -E_NOT_FOUND) {
            printf("mkdir: cannot create directory '<dir>': No such file or directory\n");
        }
    }
}

void usage(void) {
	printf("usage: mkdir [-p] [file...]\n");
	exit();
}

int main(int argc, char **argv) {

    char **o_argv = argv;
    ARGBEGIN {
	default:
		usage();
	case 'p':
		flag[(u_char)ARGC()]++;
		break;
	}
	ARGEND
    if (flag['p']) {
        mkdir(o_argv[2]);
    } else {
        mkdir(o_argv[1]);
    }

    return 0;
}