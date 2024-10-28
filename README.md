# lab6-challenge-shell 报告

## 实现不带`.b`后缀指令

* 修改了user/lib/spawn.c中的spawn()函数，当尝试打开指令执行文件失败时，在文件名后加上`.b`重新尝试打开，即可实现支持不带`.b`后缀的指令。

  ```c
  	// Step 1: Open the file 'prog' (the path of the program).
  	// Return the error if 'open' fails.
  	int fd;
  	char name[128];
  	strcpy(name, prog);
  	if ((fd = open(prog, O_RDONLY)) < 0) { // 第一次打开失败，添加.b后缀再次尝试
  		int len = strlen(name);
  		name[len] = '.';
  		name[len + 1] = 'b';
  		name[len + 2] = 0;
  		if ((fd = open(name, O_RDONLY)) < 0) { // 第二次尝试打开
  			return fd;
  		}
  	}
  ```

## 实现指令条件执行

下面分为解析和实现两个部分来解释我的实现过程。

* 第一步是解析`||`和`&&`符号，我修改了sh.c中的_gettoken()函数使其能够识别条件执行符号，并返回一个标志符。

  ```c
  	#define AND 'a'
  	#define OR  'o'	
  	if (strchr(SYMBOLS, *s)) {
  		int t = *s;
  		if (t == *(s+1)) { 
  			if (t == '|') {
  				*p1 = s;
  				s++;
  				*s++ = 0;
  				*p2 = s;
  				return OR;
  			} else if (t == '&') {
  				*p1 = s;
  				s++;
  				*s++ = 0;
  				*p2 = s;
  				return AND;
  			}
  		}
  		*p1 = s;
  		*s++ = 0;
  		*p2 = s;
  		return t;
  	}
  ```

* 实现思路是解析到条件执行符号时，fork一个子进程去执行cmd1，然后通过父子进程间的ipc通信告知父进程运行返回值，父进程判断是否执行cmd2。

  因此首先我修改了libos,c中的libmain()和exit()函数，使exit()能接受一个参数（即用户main函数运行返回值），然后把这个参数发送给父进程。

  ```c
  #include <lib.h>
  #include <mmu.h>
  void exit(int r) {
  	// After fs is ready (lab5), all our open files should be closed before dying.
  #if !defined(LAB) || LAB >= 5
  	close_all();
  #endif
  	ipc_send(env->env_parent_id, r, 0, 0);
  	syscall_env_destroy(0);
  	user_panic("unreachable code");
  }
  
  const volatile struct Env *env;
  extern int main(int, char **);
  
  void libmain(int argc, char **argv) {
  	int r;
  	// set env to point at our env structure in envs[].
  	env = &envs[ENVX(syscall_getenvid())];
  
  	// call user main routine
  	r = main(argc, argv);
  	exit(r);
  }
  ```

  同时修改了wait()函数，使其能接收到子进程的返回值。

  ```c
  int wait(u_int envid) {
  	const volatile struct Env *e;
  
  	int r;
  	u_int id = 0;
  	e = &envs[ENVX(envid)];
  	while (e->env_id == envid && e->env_status != ENV_FREE) {
  		r = ipc_recv(&id, NULL, NULL);
  		if (id == envid) {
  			return r; // 返回子进程的退出状态
  		}
  		syscall_yield();
  	}
  
      return r; // 如果没有接收到子进程的退出状态，返回错误
  }
  ```

  接下来就是在sh.c的parsecmd()中添加对条件执行的实现过程。以`||`为例：

  ```c
  static int nocmd = 0;	
  /* 省略…………………… */
  case OR:
  			nocmd = 0; 
  			*rightpipe = fork(); // 子进程返回0， 父进程返回子进程pid
  			if (*rightpipe == 0) { 
  				return argc; 	//执行cmd1
  			} else {
  				u_int id;
  				m = wait(*rightpipe); //收到子进程的返回值
  				if (m == 0) {	// 若为&&， 此处换成 m!=0即可
  					nocmd = 1; 	// cmd2不执行
  				}
  				return parsecmd(argv, rightpipe, back);
  			}
  			break;
  ```

  代码中的nocmd表示下一条命令是否执行（1则不执行，0则执行）。

  ```c
  	case 'w':
  			if (nocmd) {
  				break;
  			}
  			/* 以下省略 */
  ```

## 实现更多指令

### touch

* 新建touch.c文件，主要难点在于实现用户态下的**create()**，代码如下：

  ```c
  #include <lib.h>
  
  int touch(char *path) {
      int r;
      char temp[128];
      strcpy(temp, path);
      r = create(temp, FTYPE_REG);
      if (r < 0 && r != -E_FILE_EXISTS) {
          printf("touch: cannot touch '%s': No such file or directory\n", path);
          return -1;
      }
      return 0;
  }
  
  int main(int argc, char **argv) {
  	return touch(argv[1]);
  }
  ```

* 其中create()函数是定义在用户态的接口函数，其功能是创建一个指定类型和文件名的新文件。实现上与remove()等函数类似，通过fsipc通信通知fs服务线程。

  ```c
  // file.c
  int create(const char *path, u_int ftype) {
  	return fsipc_create(path, ftype);
  }
  // fsreq.h
  struct Fsreq_create {
  	char req_path[MAXPATHLEN];
  	u_int req_ftype;
  };
  // fsipc.c
  int fsipc_create(const char *path, u_int ftype) {
  	if (strlen(path) == 0 || strlen(path) > MAXPATHLEN) {
  		return -E_BAD_PATH;
  	}
  	struct Fsreq_create *req = (struct Fsreq_create *)fsipcbuf;
  	strcpy(req->req_path, path);
  	req->req_ftype = ftype;
  	return fsipc(FSREQ_CREATE, req, 0, 0);
  }
  ```

  在fs/serv.c中添加serve_create()函数，调用file_create()实现文件创建。

  ```c
  void serve_create(u_int envid, struct Fsreq_create *rq) {
  	int r;
  	struct File *f;
  	r = file_create(rq->req_path, &f);
  	if (r < 0) {
  		ipc_send(envid, r, 0, 0);
  		return;
  	}
  	f->f_type = rq->req_ftype;
  	ipc_send(envid, 0, 0, 0);
  }
  ```

### mkdir

* 新建mkdir.c文件，主要难点在于**递归创建父目录**， 代码如下：

  ```c
  #include <lib.h>
  
  int flag[256];
  
  int mkdir(char *path) {
      int r;
      char temp[128];
      strcpy(temp, path);
      if (flag['p']) {
          r = create(temp, FTYPE_DIR);
          // 忽略错误，若目录已存在则退出，若父目录不存在则递归创建目录
          if (r == -E_FILE_EXISTS) {
              return 0;
          }
          if (r == -E_NOT_FOUND) {
              // 递归创建父目录
              char *p = temp;
              if (*p == '/') p++;
              while (*p) {
                  while (*p && *p != '/') p++; // 找到下一个/，即一个目录
                  *p = '\0';
                  r = create(temp, FTYPE_DIR); // 每次都是从路径开头出发
                  *p = '/';
                  p++;
              }
          }
      } else {
          r = create(temp, FTYPE_DIR);
          if (r == -E_FILE_EXISTS) {
              printf("mkdir: cannot create directory '%s': File exists\n", path);
              return -1;
          }
          if (r == -E_NOT_FOUND) {
              printf("mkdir: cannot create directory '%s': No such file or directory\n", path);
              return -1;
          }
      }
      return 0;
  }
  
  void usage(void) {
  	printf("usage: mkdir [-p] [file...]\n");
  	exit(1);
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
          return mkdir(o_argv[2]);
      } else {
          return mkdir(o_argv[1]);
      }
  }
  ```

  其中我使用了`ARGBEGIN`和`ARGEND`这一对宏来处理选项的解析。

* 如果无选项，则调用create()函数创建一个目录类型的文件

* 如果有`-p`选项，则：

  * 当目录存在时直接`return 0`
  * 当目录不存在时递归创建目录，具体方法是从路径开头开始，解析到一个`/`时表明发现一个目录，然后尝试创建这个目录，然后继续解析，一直循环直到路径结束。

### rm

* 新建rm.c，主要修改的点在于如何设置**删除的权限**，代码如下：

  ```c
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
      } else if (flag['f']) {
          r = remove(temp, 0);
          return 0;
      } else {
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
  ```

* 这里我修改了user/lib/file.c下的remove函数，增加了权限位`onlyf`，表明**只有文件类型可以被删除**。相应的相关函数全部增加这一参数。

  ```c
  // user/lib/file.c
  int remove(const char *path, u_int of) { // 新增of
  	return fsipc_remove(path, of); 
  }
  // fsreq.h
  struct Fsreq_remove {
  	char req_path[MAXPATHLEN];
  	u_int req_of; // 新增
  };
  // fsipc.c
  int fsipc_create(const char *path, u_int ftype) { // 新增of
  	if (strlen(path) == 0 || strlen(path) > MAXPATHLEN) {
  		return -E_BAD_PATH;
  	}
  	struct Fsreq_create *req = (struct Fsreq_create *)fsipcbuf;
  	strcpy(req->req_path, path);
  	req->req_ftype = ftype;
  	return fsipc(FSREQ_CREATE, req, 0, 0);
  }
  ```

  修改fs/serv.c和fs.c中相应函数。

  ```c
  // error.h
  #define E_NOT_REMOVE 14
  
  // serv.c
  void serve_remove(u_int envid, struct Fsreq_remove *rq) {
  	int r;
  	r = file_remove(rq->req_path, rq->req_of);
  	ipc_send(envid, r, 0, 0);
  }
  // fs.c
  int file_remove(char *path, u_int onlyf) { // 新增of
  	int r;
  	struct File *f;
  	if ((r = walk_path(path, 0, &f, 0)) < 0) {
  		return r;
  	}
  	/* 新增 */
      // 当只允许普通文件被删除且目标文件类型不符时，不删除，返回错误码-E_NOT_REMOVE
  	if (onlyf && f->f_type != FTYPE_REG) {
  		return -E_NOT_REMOVE;
  	}
      /* 不变 */
  	file_truncate(f, 0);
  	f->f_name[0] = '\0';
  	file_flush(f);
  	if (f->f_dir) {
  		file_flush(f->f_dir);
  	}
  	return 0;
  }
  ```

* 因此当只允许文件被删除时就使用remove(path, 1)，若允许删除目录，则使用remove(path, 0)。

* 其他错误情况下的输出则根据remove返回值来决定。

## 实现反引号

* 由于只考虑echo的输出，考虑到echo的效果是讲echo后面的内容打印到标准输出，而反引号功能则是指令替换，可以直接绕过`echo`，**只执行反引号中的内容**，效果上等同于**先执行反引号命令再echo出执行结果**。

* 因此直接修改parsecmd()，遇到反引号时就将反引号中的内容替换掉原先的`echo`。

  ```c
  #define SYMBOLS "<|>&;()`" // 将反引号也添加进SYMBOLS
  
  static int flag = 0;
  /* 省略若干行 */
  case '`' :
  	if (flag) {
  		flag = 0;
  		return argc;
  	}
  	argc--;
  	flag = 1;
  	break;
  ```

  此处的`flag`初始为0，当`flag == 1 `表明当前解析的内容位于反引号内，因此当再次遇见反引号时即可认为反引号内的命令输入结束，进而正常执行该命令即可。

## 实现注释功能

* 修改sh.c中的_gettoken()，遇见`#`时就结束。

  ```c
  if (*s == '#') { return 0; }
  ```

## 实现历史指令

* 首先是指令保存的实现，shell通过readline()将命令存储到`buf`后，通过`save_history()`实现将指令保存至`.mosh_history`中。

  ```c
  readline(buf, sizeof buf); // 读取命令
  save_history(buf, 0);	   // 保存命令
  ```

  首先通过类似追加重定向的方式打开`.mosh_history`文件（关于**追加重定向**的实现可以移步至后面的说明），如果打不开则创建一个，并将初始四条历史指令添加进`.mosh_history`中。

  `offset[128]`数组存储了每个命令（从1开始计数）在`.mosh_history`中的偏移量。`offset[i-1]`表示第`i`条命令的文件指针偏移。`offset[i] - offset[i-1]`则表明第`i`命令的长度(包含换行符)。

  `historyLength`存储了下一条命令的编号，`historyLength - 1`就是当前存储在`.mosh_history`中的命令数量。

  `init`参数表明初次创建`.mos_history`则直接返回，否则继续执行后面的行为。

  ```c
  char inithis[128] = "echo `ls | cat`\necho meow # comment\nhistory\nhistory | cat\n";
  int offset[128];
  int historyLength = 0;
  
  void save_history(char *cmd, int init) {
  	char tmp[128];
  	int fd, r;
  	strcpy(tmp, cmd);
  	if ((fd = open("/.mosh_history", O_RDWR | O_APPEND)) < 0) {
  		create("/.mosh_history", FTYPE_REG);
  		if ((fd = open("/.mosh_history", O_RDWR | O_APPEND)) < 0) {
  			debugf("failed to open '%s'\n", "/.mosh_history");
  			exit(1);
  		} else {
  			if ((r = write(fd, inithis, strlen(inithis))) != strlen(inithis)) {
  				debugf("write error: %s\n", inithis);
  			}
  			offset[0] = 0;
  			offset[1] = offset[0] + strlen("echo `ls | cat`") + 1;
  			offset[2] = offset[1] + strlen("echo meow # comment") + 1;
  			offset[3] = offset[2] + strlen("history") + 1;
  			offset[4] = offset[3] + strlen("history | cat") + 1;
  			historyLength = 5;
  		}
  	}
      if (init) {
  		return;
  	}
      /* 省略 */
  }
  ```

  接下来就该将新命令存储进历史命令文件中，但存储之前先检查是否已满（`.mosh_history`中最多存储20条历史指令）。若已满则需要将最早进入的一条历史指令删除掉。

  我的实现思路是创建一个临时历史文件`.temp_history`，将当前历史命令文件中的2-20条存入临时文件，然后清空`.mosh_history`，将临时文件中的内容写入`.mosh_history`中。

  ```c
  #define HISTFILESIZE 20 // 历史文件存储最大命令数
  
  void save_history(char *cmd, int init) {
  	/* 接上段代码 */
  	if (historyLength > HISTFILESIZE) { // 满了
  		int n = 1;
  		int f; // 打开临时文件
  		char buf[128];
  		r = create("/.temp_history", FTYPE_REG);
  		if ((f = open("/.temp_history", O_RDWR | O_TRUNC)) < 0) {
  			debugf("failed to open '%s'\n", "/.temp_history");
  			exit(1);
  		}
  		seek(fd, offset[1]); // 移动文件指针跳过第一条命令
  		// 将后19个写入临时文件
  		while (n < HISTFILESIZE) {
  			memset(buf, 0, 128);
  			if ((r = read(fd, buf, offset[n + 1] - offset[n])) != offset[n + 1] - offset[n]) {
  				debugf("read error from his: %s by %d\n", buf, n);
  				exit(1);
  			}
  			if ((r = write(f, buf, strlen(buf))) != strlen(buf)) {
  				debugf("write error to temp: %s\n", cmd);
  				exit(1);
  			}
  			
  			n++;
  		}
  		close(fd);
  		// 清空历史记录
  		if ((fd = open("/.mosh_history", O_WRONLY | O_TRUNC)) < 0) {
  			debugf("failed to open '%s'\n", "/.mosh_history");
  			exit(1);
  		}
  		// 重新写入历史记录
  		seek(f, 0);
  		n = 1;
  		while(n < HISTFILESIZE) {
  			memset(buf, 0, 128);
  			if ((r = read(f, buf, offset[n + 1] - offset[n])) != offset[n + 1] - offset[n]) {
  				debugf("read error from temp: %s by  %d\n", buf, n);
  				exit(1);
  			}
  			if ((r = write(fd, buf, strlen(buf))) != strlen(buf)) {
  				debugf("write error to his: %s\n", buf);
  				exit(1);
  			}
  			offset[n] = offset[n - 1] + strlen(buf);
  			n++;
  		}
  		close(f); 					 // 关闭临时文件
  		remove("/.temp_history", 0); // 删除临时文件
  		historyLength--; // 清理掉最早的记录后将historyLength--
  	}
  	/* 省略 */
  }
  ```

  现在就可以将新命令写入`.mosh_history`了。另外需要额外补充换行符。

  ```c
  void save_history(char *cmd, int init) {	
      /* 接上段代码 */
  	if ((r = write(fd, tmp, strlen(tmp))) != strlen(tmp)) {
  		debugf("write error: %s\n", tmp);
  	}
  
  	if ((r = write(fd, "\n", 1)) != 1) {
  		debugf("write error: '\\n'\n");
  	}
  	offset[historyLength] = offset[historyLength - 1] + strlen(tmp) + 1;
  	historyLength++;
  	close(fd);
  }
  ```

* 保存好之后就是实现`up`和`down`键切换命令。

  修改readline()使其能够识别`up`和`down`键。具体方法是采用linux移动光标的命令：

  * 向上移动`i`：\033[`i`A
  * 向下移动`i`：\033[`i`B
  * 向右移动`i`：\033[`i`C
  * 向左移动`i`：\033[`i`D

  ```c
  	int save = 0;
  	char curcmd[128]; // 保存控制台输入的指令
  	int curlen = 0;   // 当前控制台指令的长度
  	if (buf[i] == '\033') {
  			char h;
  			read(0, &h, 1); // 读取一个字符
  			if (h == '[') {
  				read(0, &h, 1);
  				char cmd[128];
  				buf[i] = 0;
  				if (!save) {
  					memset(curcmd, 0, 128);
  					strcpy(curcmd, buf); // 保存当前控制台输入的指令
  					curlen = strlen(buf);
  					save++;
  				}
  				switch (h) {
  					case 'A':
  						/*------- up -------*/
  						break;
  					case 'B':
  						/*------ down ------*/
  						break;
  				}
  			}
  		}
  ```

  当收到`up`键时，去向上寻找历史命令；收到`down`键时向下寻找历史命令。

  `save`用于标记是否保存了当前输入台输入的指令（还未运行），在第一次出现`up`或者`down`时会自动保存一下当前指令到`curcmd`中。

  获取`.mos_history`中的历史命令的方法函数是read_history()。参数1表示目标历史指令编号，将结果保存在参数2中。

  ```c
  void read_history(int index, char *cmd) {
  	int r;
  	int fd;
  	char buf[128];
  	if ((r = open("/.mosh_history", O_RDONLY)) < 0)
  	{
  		debugf("can't open file .mosh_history: %d\n", r);
  		exit(1);
  	}
  	fd = r;
  	if (index > 0 && index <= HISTFILESIZE) { // index表示第几条历史指令
  		int len = offset[index] - offset[index - 1];
  		memset(buf, 0, 128);
  		seek(fd, offset[index - 1]); // 移动指针至目标指令位置
  		if ((r = read(fd, buf, len - 1)) != len - 1) { //这里len - 1是为了丢掉换行符
  			debugf("read history cmd error: %s\n", buf);
  			exit(1);
  		}
  		buf[len] = 0;
  		strcpy(cmd, buf);
  	}
  }
  ```

  在readline()中定义一个his指向当前指令编号（从`.mos_history`第一条到控制台作为最后一条）。以`up`为例，当向上寻找命令时，`his--`，然后调用`read_history()`将指令存储在`buf`中。随后通过移动光标将控制台输出替换为新的`buf`，同时修改`curlen`以及光标`i`。

  ```c
  case 'A':
  	printf("\033[1B"); // 光标移下来
  	if (his > 1) his--; // 若his == 1，则说明读到最后一条了
  	read_history(his, buf); 
  	// 移动光标
  	if (i > 0) printf("\033[%dD", i);
  	printf("%s", buf); // 输出cmd
  	if (strlen(buf) < curlen) { // 将前一条命令多出来的位置替换为空格
  		for (int j = 0; j < curlen - strlen(buf); j++) {
  			printf(" ");
  		}
  		printf("\033[%dD", curlen - strlen(buf)); // 移动光标至当前指令结尾处
  	}
  	curlen = strlen(buf);
  	i = curlen - 1;
  	break;
  ```

  `down`键也类似。

  ```c
  case 'B':
  	if (his == historyLength - 1) { // 回到控制台指令
  		his++;
  		strcpy(buf, curcmd);
  	} else if (his == historyLength) {
  		break;
  	} else {
  		his++;
  		read_history(his, buf);
  	}
  						
  	// 移动光标
  	if (i > 0) printf("\033[%dD", i);
  	printf("%s", buf); // 输出cmd
  	if (strlen(buf) < curlen) {
  		for (int j = 0; j < curlen - strlen(buf); j++) {
  			printf(" ");
  		}
  		printf("\033[%dD", curlen - strlen(buf));
  	}
  	curlen = strlen(buf);
  	i = curlen - 1;
  	break;
  ```

* `history`命令的实现是通过将其替换成`cat .mos_history`来实现的。需要修改parsecmd().

  ```c
  case 'w':
  	if (nocmd) {
  		break;
  	}
  	if (argc >= MAXARGS) {
  		debugf("too many arguments\n");
  		exit(1);
  	}
  	if (strcmp(t, "history") == 0) {
  		argv[argc++] = "cat";
  		argv[argc++] = "/.mosh_history";
  	} else {
  		argv[argc++] = t;
  	}
  	break;
  ```

## 实现一行多指令

* 修改parsecmd()，遇到`;`时fork一个子进程执行分号前的命令，父进程等待子进程执行完后再继续执行。

  ```c
  case ';':
  	*rightpipe = fork();
  	if (*rightpipe == 0) {
  		return argc; 
  	} else {
  		wait(*rightpipe);
  		return parsecmd(argv, rightpipe, back);
  	}
  	break; 
  ```

## 实现追加重定向

* 修改parsecmd()，识别到`>`后寻找下一个`>`，若找到说明是追加重定向，然后通过`O_APPEND`的权限打开：在这个权限下，文件偏移指针会移动至文件末尾，实现追加重定向。同时若文件不存在时需要主动创建文件。

  ```c
  case '>':
  	if ((r = gettoken(0, &t)) != 'w') {
  		if (r == '>') { // 追加重定向
  			if (gettoken(0, &t) != 'w') {
  				debugf("syntax error: >> not followed by word\n");
  				exit(1);
  			}
  			if ((r = open(t, O_WRONLY | O_APPEND)) < 0) {
  				create(t, FTYPE_REG);
  			}
  			if ((r = open(t, O_WRONLY | O_APPEND)) < 0) {
  				debugf("failed to open '%s'\n", t);
  				exit(1);
  			} else {
  				fd = r;
  				dup(fd, 1);
  				close(fd);
  				break;
  			}
  			user_panic(">> redirection not implemented");
  		} else {
  			debugf("syntax error: > not followed by word\n");
  			exit(1);
  		}
  	}
  	/* 以下省略>重定向实现 */
  ```

* `O_APPEND`这个权限定义在user/include/lib.h中。

  ```c
  #define O_APPEND 0x0008  /* append to the end of the file */
  ```

  为实现这一权限我修改了fs/serv.c中的serve_open()函数，添加如下代码：

  ```c
  if (rq->req_omode & O_APPEND) {
  	if ((r = file_seek(ff, f->f_size)) < 0) {
  		ipc_send(envid, r, 0, 0);
  		return;
  	}
  }
  ```

  其中`file_seek()`函数作用是将一个`Filefd`类型的变量中的文件偏移指针移动若干位。具体实现如下：

  ```c
  // fs/fs.c
  int file_seek(struct Filefd *ff, u_int offset) {
  	struct File *f;
  	f = &ff->f_file;
      if (offset < 0 || offset > f->f_size) {
          return -E_INVAL; // Invalid offset
      }
      struct Fd *fd = (struct Fd*)ff;
  	fd->fd_offset = offset;
      return 0;
  }
  ```

  从而实现了`fd_offset`的移动。保证了被重定向的文件内容不会被覆盖。

* 此外我还修改了普通重定向的打开权限，增加了`O_TRUNC`，保证以`>`重定向方式打开的文件指针移动到最开始。

## 实现引号支持

* 修改sh.c中的_gettoken()，遇到`"`时继续将两个引号之间的部分解析成一个字符串。

  ```c
  if (*s == '\"') {
  	s++;
  	*p1 = s;
  	while (*s && *s != '\"') {
  		s++;
  	}
  	*s++ = 0;
  	*p2 = s;
  	return 'w';
  }
  ```

## 实现前后台任务管理

* 首先支持对`&`的解析，parsecmd()找到`&`时将传入的`*back`赋值为1，表明此指令需要后台执行。

  ```c
  case '&':
  	*back = 1;
  	return argc;
  ```

* 接着修改runcmd()，当传入的back返回0时，说明还是前台执行，则需要等待子进程运行结束；若非0，则说明后台执行，不需要等待子进程，可以直接`exit()`。这里我选择将子进程进程号传递给父进程，是为后面记录后台指令做准备。

  ```c
  void runcmd(char *s) {
  	gettoken(s, 0);
  
  	char *argv[MAXARGS];
  	int rightpipe = 0;
  	int r;
  	int back = 0; // 判断是否是后台执行
  	int argc = parsecmd(argv, &rightpipe, &back);
  	if (argc == 0) {
  		return;
  	}
  	argv[argc] = 0;
  	
  	int child = spawn(argv[0], argv);
  	close_all();
  	/* 主要修改的部分 */
  	if (child >= 0) {
  		if (back == 0) { // 前台执行，需要等待
  			r = wait(child);
  		} else { 		 // 后台执行，不需要等待
  			exit(child);
  		}
  	} else {
  		debugf("spawn %s: %d\n", argv[0], child);
  	}
  	if (rightpipe) {
  		r = wait(rightpipe);
  	}
      /* 保证异常执行的命令返回值 < 0 */
  	if (r != 0) {
  		r = -1;
  	}
  	exit(r);
  }
  ```

* 后台指令的统计通过创建一个结构体`job_t`，记录后台指令的基本信息和运行情况。然后构建一个`job_t`类型的数组来记录后台指令信息。`max_jobid`则记录了创建过的后台指令数量。

  ```c
  typedef struct job {
  	u_int jobid;
  	u_int envid;
  	char status[20];
  	char cmd[128];
  } job_t;
  
  job_t job_list[16];
  static int max_jobid = 0;
  ```

  `jobid`通过`jobid_alloc()`函数生成：

  ```c
  int jobid_alloc() {
  	static int i = 0;
  	i++;
  	return i;
  }
  ```

* 如果指令后台执行，那么子shell不会等待运行指令的子进程，而是会告知主shell运行命令的子进程的env_id，这样主shell就可以记录下后台指令的相关信息存储在`job_list`中。

  ```c
  int main(int argc, char **argv) {
  	int r;
  	int interactive = iscons(0);
  	int echocmds = 0;
  	printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
  	printf("::                                                         ::\n");
  	printf("::                     MOS Shell 2024                      ::\n");
  	printf("::                                                         ::\n");
  	printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
  	
  	/* 省略无关代码 */
  	
  	for (;;) {
  		if (interactive) {
  			printf("\n$ ");
  		}
  		save_history("anything", 1);
  		readline(buf, sizeof buf);
          save_history(buf, 0);
          
          check_jobs(); // 更新job_list中指令状态
  		/* 暂时忽略的代码 */
  
  		if ((r = fork()) < 0) {
  			user_panic("fork: %d", r);
  		}
  		char cmd[128];
  		strcpy(cmd, buf);
  		
  		if (r == 0) {
  			/* 子shell运行runcmd(buf) */
  			exit(0);
  		} else {
              // 父进程作为主shell，接收子shell的通信
  			int b;
  			b = wait(r); 
  			if (b > 0) { // 说明是后台执行，并且wait()返回值为运行命令的子进程进程id
  				int jobid = jobid_alloc();
  				max_jobid++;
  				job_list[jobid - 1].envid = b;
  				strcpy(job_list[jobid - 1].status, "Running");
  				job_list[jobid - 1].jobid = jobid;
  				strcpy(job_list[jobid - 1].cmd, cmd);
  			} 
  		}
  	}
  	return 0;
  }
  ```

* 更新`job_list`的函数是`check_jobs()`，放在循环内每次运行命令之前。内部逻辑与`wait()`类似。

  ```c
  void check_jobs() {
  	for (int i = 0; i < max_jobid; i++) {
  		if (strcmp(job_list[i].status, "Running") == 0) {
  			struct Env * e;
  			e = &envs[ENVX(job_list[i].envid)];
  			if (e->env_id != job_list[i].envid || e->env_status == ENV_FREE) {
  				strcpy(job_list[i].status, "Done");
  			}
  		}
  	}
  }
  ```

* 接着是实现`jobs`，`fg`，`kill`三个内部命令，全部在子进程中执行。三个命令都通过各自的`do_*`函数实现。

  ```c
  		if (r == 0) {
  			if (strncmp("jobs", buf, 4) == 0) {
  				do_jobs();
  				exit(0);
  			} 
  			else if (strncmp("fg", buf, 2) == 0) {
  				int id;
  				char tmp[128];
  				strcpy(tmp, buf + 3);
  				if ((id = myatoi(tmp)) > 0) {
  					do_fg(id);
  				} else {
  					printf("fg: job (%d) do not exist\n", id);
  				}
  				exit(0);
  			} 
  			else if (strncmp("kill", buf, 4) == 0) {
  				int id;
  				char tmp[128];
  				strcpy(tmp, buf + 5);
  				if ((id = myatoi(tmp)) > 0) {
  					do_kill(id);
  				} else {
  					printf("fg: job (%d) do not exist\n", id);
  				}
  				exit(0);
  			}
  			else {
  				runcmd(buf);
  				exit(0);
  			}
  			
  		} else {
  			/* 省略父进程代码 */
  		}
  ```

  其中`strncmp`和`myatoi`是我额外添加的库函数，定义在`string.h`中，实现在`string.c`中。`strncmp`是比较两个字符串前n个字符是否相等，`myatoi`是将数字构成的字符串转换成对应的数字。

  ```c
  int strncmp(const char *s1, const char *s2, int n) {
      while (n--) {
          if (*s1 != *s2) {
              return *(u_char *)s1 - *(u_char *)s2;
          }
          if (*s1 == '\0') {
              return 0;
          }
          s1++;
          s2++;
      }
      return 0;
  }
  
  int myatoi(char *str) {
      int num = 0;
      int sign = 1;  // 默认为正数
      int i = 0;
      // 跳过前导空格
      while (str[i] == ' ') { i++; }
      // 处理符号
      if (str[i] == '-') {
          sign = -1;
          i++;
      } else if (str[i] == '+') {
          i++;
      }
      // 将字符转换为整数
      while (str[i] >= '0' && str[i] <= '9') {
          num = num * 10 + (str[i] - '0');
          i++;
      }
      // 应用符号
      return sign * num;
  }
  ```

  `do_jobs()`负责输出即可。

  ```c
  void do_jobs() {
  	for (int i = 0; i < max_jobid; i++) {
  		printf("[%d] %-10s 0x%08x %s\n", job_list[i].jobid, job_list[i].status, job_list[i].envid, job_list[i].cmd);
  	}
  }
  ```

  `do_fg()`作用是将指令带回前台，即等待指令运行完，只需要调用wait就好。这里我选择调用`waitpid()`，其功能和代码与lab6最初`wait()`函数代码一模一样，因此功能也一样，只是不具备`ipc_recv`的能力。

  ```c
  void do_fg(int id) {
  	if (id > max_jobid || id <= 0) {
  		printf("fg: job (%d) do not exist\n", id);
  		return;
  	}
  	if (strcmp(job_list[id - 1].status, "Done") == 0) {
  		printf("fg: (0x%08x) not running\n", job_list[id - 1].envid);
  		return;
  	}
  	waitpid(job_list[id - 1].envid);
  }
  ```

  `do_kill()`函数作用是直接杀死进程，并且要修改对应后台指令状态为`Done`。此处我是通过`syscall_env_destroy()`杀死的子进程，但由于该系统调用限制了只能杀死自己的子进程，而我们要杀死的进程并非是当前子shell的子进程（原来创建子进程的子shell已经被destroy了）。因此需要将内核系统调用中的`sys_env_destroy`中调用`envid2env()`传入的`perm`参数修改成0。

  ```c
  int sys_env_destroy(u_int envid) {
  	struct Env *e;
  	try(envid2env(envid, &e, 0)); // 原来是1， 现在改为0
  
  	printk("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
  	env_destroy(e);
  	return 0;
  }
  ```

  ```c
  void do_kill(int id) {
  	if (id > max_jobid || id <= 0) {
  		printf("fg: job (%d) do not exist\n", id);
  		return;
  	}
  	if (strcmp(job_list[id - 1].status, "Done") == 0) {
  		printf("fg: (0x%08x) not running\n", job_list[id - 1].envid);
  		return;
  	}
  	syscall_env_destroy(job_list[id - 1].envid);
  	strcpy(job_list[id - 1].status, "Done");
  }
  ```

## 结尾

以上就是我本次`challenge-shell`的实现，如有遗漏未结束清楚的恳请助教及老师多多包涵。如有与源码不一样的地方，多半是我来不及删去的注释或者debugf，不影响整体，望海涵。
