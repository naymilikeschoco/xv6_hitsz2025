#include "kernel/types.h"
#include "user.h"

int main(int argc, char *argv[]){
    int ctf[2], ftc[2];
    pipe(ctf);  // child to father
    pipe(ftc);  // father to child
    if(fork() == 0){
        // child
        close(ctf[0]);
        close(ftc[1]);

        char buf[16];
        read(ftc[0], buf, sizeof(buf));
        int parent_pid = atoi(buf);
        int child_pid = getpid();
        printf("%d: received ping from pid %d\n", child_pid, parent_pid);

        // deliver child_pid to parent!
        char message[16];
        itoa(child_pid, message);
        write(ctf[1], message, strlen(message) + 1);

        close(ctf[1]);
        close(ftc[0]);
        exit(0);
    }
    else{
        // parent
        close(ctf[1]);
        close(ftc[0]);
        //according to the child's behaviour, parent should sends its pid to child firstly
        char message[16];
        int parent_pid = getpid();
        itoa(parent_pid, message);
        write(ftc[1], message, strlen(message) + 1);

        char buf[16];
        read(ctf[0], buf, sizeof(buf));
        int child_pid = atoi(buf);
        printf("%d: received pong from pid %d\n", parent_pid, child_pid);
        close(ctf[0]);
        close(ftc[1]);
        wait(0);
    }
    exit(0);
}