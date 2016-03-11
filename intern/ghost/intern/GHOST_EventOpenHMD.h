#ifndef __GHOST_EVENTOPENHMD_H_
#define __GHOST_EVENTOPENHMD_H_

#include "GHOST_Event.h"

class GHOST_EventOpenHMD : public GHOST_Event
{
public:
    GHOST_EventOpenHMD (GHOST_TUns64 time, GHOST_IWindow *window)
		: GHOST_Event(time, GHOST_kEventHMD, window)
	{
		m_data = &m_orientationData;
	}

protected:
    GHOST_TEventOpenHMDData m_orientationData;

};

#endif // __GHOST_EVENTOPENHMD_H_
