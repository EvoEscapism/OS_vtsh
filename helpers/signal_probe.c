#include <signal.h>

int main(void)
{
    /* The parent shell should report 128 + SIGTERM as this command's status. */
    (void)raise(SIGTERM);
    return 1;
}
