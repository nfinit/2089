/* probe.c - does a Motif program compile and link here?  Built and thrown
   away by "make x11" to decide whether to build xm2089.  Never run. */

#include <Xm/Xm.h>

int main(void)
{
    return XmStringCreateLocalized == 0;
}
