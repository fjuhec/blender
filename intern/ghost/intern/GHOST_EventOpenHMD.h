#ifndef __GHOST_EVENTOPENHMD_H_
#define __GHOST_EVENTOPENHMD_H_

#include "GHOST_Event.h"

class GHOST_EventOpenHMD : public GHOST_Event
{
public:
	GHOST_EventOpenHMD(GHOST_TUns64 time, GHOST_TEventOpenHMDSubTypes subtype, GHOST_IWindow *window)
		: GHOST_Event(time, GHOST_kEventHMD, window)
	{
		m_OpenHMDEventData.subtype = subtype;
		m_data = &m_OpenHMDEventData;
	}

protected:
	GHOST_TEventOpenHMDData m_OpenHMDEventData;
};

#endif // __GHOST_EVENTOPENHMD_H_
