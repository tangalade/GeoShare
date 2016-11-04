#include <map>
#include <vector>
#include <iostream>

#include "clock.h"
#include <sys/time.h>

std::map<std::string,timeval> timing_start;
std::vector<CLOCK_VAL> clock_v;

void clock_start(std::string key)
{
  timeval start;
  gettimeofday(&start,NULL);
  timing_start.insert(CLOCK_VAL(key,start));
}

void clock_end(std::string key)
{
  timeval end;
  if ( timing_start.find(key) == timing_start.end() )
    return;
  gettimeofday(&end,NULL);
  clock_v.push_back(CLOCK_VAL(key,timeval_subtract(end,timing_start[key])));
}

void clock_reset()
{
  timing_start.clear();
  clock_v.clear();
}

CLOCK_VAL clock_get(int idx)
{
  return clock_v[idx];
}

size_t clock_size()
{
  return clock_v.size();
}

timeval timeval_subtract (timeval x, timeval y)
{
  struct timeval result;
  /* Perform the carry for the later subtraction by updating y. */
  if (x.tv_usec < y.tv_usec) {
    int nsec = (y.tv_usec - x.tv_usec) / 1000000 + 1;
    y.tv_usec -= 1000000 * nsec;
    y.tv_sec += nsec;
  }
  if (x.tv_usec - y.tv_usec > 1000000) {
    int nsec = (x.tv_usec - y.tv_usec) / 1000000;
    y.tv_usec += 1000000 * nsec;
    y.tv_sec -= nsec;
  }

  result.tv_sec = x.tv_sec - y.tv_sec;
  result.tv_usec = x.tv_usec - y.tv_usec;

  return result;
}
timeval timeval_divide (timeval x, int divisor)
{
  x.tv_usec /= divisor;
  x.tv_sec *= 1000000;
  x.tv_sec /= divisor;
  x.tv_usec += x.tv_sec % 1000000;
  x.tv_sec /= 1000000;
  return x;
}
