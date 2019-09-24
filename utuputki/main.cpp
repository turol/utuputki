#include <cstring>

#include <unistd.h>

#include "utuputki/Utuputki.h"


int main(int /* argc */, char *argv[]) {
	bool reExec = false;
	{
		utuputki::Utuputki utuputki;

		utuputki.run();

		reExec = utuputki.shouldReExec();
	}

	if (reExec) {
		printf("Re-execing...\n");
		int result = execl(argv[0], argv[0], nullptr);
		if (result < 0) {
			printf("execl failed: %d %s\n", errno, strerror(errno));
		}
	}
}
