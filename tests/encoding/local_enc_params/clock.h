#include <string>

#include <sys/time.h>

#define CLOCK_UNIT timeval
#define CLOCK_VAL std::pair<std::string,timeval>

void clock_start(std::string key);
void clock_end(std::string key);
void clock_reset();
CLOCK_VAL clock_get(int idx);
size_t clock_size();

timeval timeval_subtract (timeval x, timeval y);
timeval timeval_divide (timeval x, int divisor);
