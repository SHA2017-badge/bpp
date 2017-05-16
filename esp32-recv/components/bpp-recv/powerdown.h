#ifndef POWERDOWN_H
#define POWERDOWN_H

void powerHold(int ref);
void powerCanSleepFor(int ref, int delayMs);
void powerCanSleep(int ref);

#endif