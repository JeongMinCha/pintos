#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
    printf ("%s\n", argv[1]);
    printf ("hello world\n");

    return 0;
}
