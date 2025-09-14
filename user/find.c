#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *name){
    char buf[512], *p;         // 缓冲区用于构建完整路径
    int fd;                    // 文件描述符
    struct dirent de;          // 目录项结构
    struct stat st;            // 文件状态结构

    // 打开路径
    if ((fd = open(path, 0)) < 0){
        printf("find: cannot open %s\n", path);
        return;
    }
    
    // 获取文件状态
    if (fstat(fd, &st) < 0){
        printf("find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    char *last_name = path;
    for (char *temp = path; *temp; temp++) {
        if (*temp == '/') {
            last_name = temp + 1;    // 提取出路径中的文件名或者目录名部分进行比较
        }
    }

    switch (st.type) {
        case T_FILE:
        // 如果是文件，检查是否匹配目标文件名
            if (strcmp(last_name, name) == 0) {
                printf("%s\n", path);
            }
            break;

        case T_DIR:
        // 如果是目录，遍历目录项
            // 检查目录本身是否匹配
            if (strcmp(last_name, name) == 0) {
                printf("%s\n", path);
            }

            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
                printf("find: path too long\n");
                break;
            }
            // 下面递归查找
            // 构建基础路径
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';
            // 遍历目录中的所有项
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0) continue; // 跳过空目录项
                if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;  // 跳过 "." 和 ".."，避免无限递归
                // 构建完整子路径
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;  // 确保字符串以null结尾
                // 递归调用find函数
                find(buf, name);
            }
            break;
    }
    close(fd);
}


int main(int argc, char *argv[]){
    if (argc < 3) {
        printf("Error! Usage: find <path> <filename>\n");
        exit(1);
    }
    char *path = argv[1];
    char *name = argv[2];
    find(path, name);
    exit(0);
}