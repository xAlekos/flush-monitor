#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	setenv("GDK_BACKEND", "x11", 1);
	execl("/usr/bin/zenity", "zenity", "--question", "--timeout=5",
	      "--width=430", "--height=180",
	      "--title=VICTIM - legitimate dialog", "--text=Victim dialog.",
	      (char *)NULL);
	perror("exec /usr/bin/zenity");
	return 1;
}
