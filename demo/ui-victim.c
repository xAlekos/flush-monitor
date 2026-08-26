#include <stdlib.h>

int main(void)
{
	return system("zenity --question --timeout=5 --width=430 "
		      "--title='VICTIM — legitimate dialog' "
		      "--text='Victim dialog.' "
		      "2>/dev/null") == -1;
}
