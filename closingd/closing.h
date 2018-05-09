#ifndef LIGHTNING_LIGHTNINGD_CLOSING_H
#define LIGHTNING_LIGHTNINGD_CLOSING_H

#include <lightningd/channel.h>

void close_channel(struct channel* channel, 
					u8 *msg,					
					void (*billboardcb)(void *channel, bool perm, const char *happenings));

#endif