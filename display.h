#ifndef MOVGRAB_DISPLAY_H
#define MOVGRAB_DISPLAY_H

#include "common.h"

extern int DisplayTitleWidth;

int DisplayAvailableFormats(ListNode *Vars, char *Formats);
void DisplayProgress(const char *FullTitle, const char *Format, double bytes_read, double DocSize, int PrintName);

#endif

