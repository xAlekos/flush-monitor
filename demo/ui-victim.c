#include <stdlib.h>

int main(void)
{
	return system("zenity --question --timeout=5 --width=430 "
		      "--title='VICTIM — legitimate dialog' "
		      "--text='Controlled victim event. No credentials are requested.' "
		      "2>/dev/null") == -1;
}
