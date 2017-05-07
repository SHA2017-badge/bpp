/*
Tool to sync one file to a remote one, using broadcast commands only.

It works like this: If we have a trickle of about 10KByte/second (or 1% of an 11MBit 
connection), it will take about 6 minutes to update an 4MB image. Thus, it's not 
smart to just stream that out: it will take a while for the thing to update, even 
if most of the stuff we have in the image is still very recent. Thus, we resort to 
a more pragmatic option of updating stuff: the most recently changed stuff is sent 
out most often, while the more static data is sent out less often.

Maybe the idea is to send out bitmaps. It comes down to this: Every update has a 
certain serial; an 'update' essentially is a steady-state of the file system. This
means there are multiple sectors that can have changed from update to update.

Now, we can go broadcast all sectors plus the update number they now have. This 
allows all badges to happily update to a certain update number provided they catch
all sectors of the update.

Some sectors don't change from update to update. How do we fix this? Well, we send out
bitmaps. Basically, we can say 'If you have these sectors with an update id of x or 
later, they are still current and you can immediately give them a new update ID'. The 
badges will need to wait for the rest of the updates to trickle in, but we can schedule
them later if needed.

Badges will wake every 60 sec. Updating takes (say) 2 sec max. This means the duty cycle
is 1/30th. Say 150mA for an update -> 5mA continuous use. For an 800mA battery, this is 
almost 7 days.

What we can do is intersperse the packet streams with a packet 'Next catalog in x mS'. Then,
after x ms, we can send bitmaps: 1 for the updates since 1 min ago, 1 since 5 min ago, 1 
since an hour ago etc. We then send these sectors in sequence: from last-updated to 
least-last-updated. We limit this to take a certain percentage of the available bandwidth 
for the minute, so we also have time and space to disperse other messages. In this 'remaining'
space, we can disperse 'live' messages as well as slowly trickle the older sectors to the 
devices that were away for a longer time. (Maybe even put a pointer to when these will start 
in a packet in the catalog.)


So basically:
:00
Bitmap for changes since 1 min ago
Bitmap for changes since 5 min ago
Bitmap for changes since 30 min ago
Time marker: streaming of older data starts at :25
Change 1 (1 min ago)
Change 2
Next catalog starts in x msec
Change 3
...
Next catalog starts in x msec
Change X (30 min ago
:25
Rotating older changes
Next catalog starts in x msec
More rotating older changes

:00
Bitmap etc

Nice thing: a bitmap fits in 128 bytes or so. Which means we can add a fairly large amount of bitmaps.
Idea: just schedule a fixed amount of updates and do a fixed amount of bitmaps as in-betweens? Hmm,
this does mean each badge will need to wait for a full update when the image changes >600K... Maybe 
implement a special mode for that.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "sendif.h"
#include "structs.h"
#include "crc16.h"










