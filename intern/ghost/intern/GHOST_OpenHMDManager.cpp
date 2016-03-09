#include "GHOST_OpenHMDManager.h"

// TODO replace with blender internal openhmd files
#include <openhmd/openhmd.h>

#include "GHOST_EventOpenHMD.h"
#include "GHOST_WindowManager.h"

GHOST_OpenHMDManager::GHOST_OpenHMDManager(GHOST_System& sys)
    : m_system(sys),
      m_available(false),
      m_context(NULL),
      m_device(NULL)
{
	m_context = ohmd_ctx_create();
    if (m_context != NULL) {

        int num_devices = ohmd_ctx_probe(m_context);
        if (num_devices > 0) {
            m_available = true;

            printf("Found %i OpenHMD devices", num_devices);
            for (int i = 0; i < num_devices; ++i) {
                printf("Device %i\n", i);
                printf("vendor: %s\n", ohmd_list_gets(m_context, i, OHMD_VENDOR));
                printf("product: %s\n", ohmd_list_gets(m_context, i, OHMD_PRODUCT));
                printf("path: %s\n", ohmd_list_gets(m_context, i, OHMD_PATH));
            }

            //can't fail?
            m_device = ohmd_list_open_device(m_context, 0);
        }
        else {
            printf("No available devices in OpenHMD Context\n");

            ohmd_ctx_destroy(m_context);
            m_context = NULL;
        }
    }
    else {
        printf("Failed to create OpenHMD Context\n");
    }


    /*
	ohmd_device_geti(hmd, OHMD_SCREEN_HORIZONTAL_RESOLUTION, &ival);
	ohmd_device_geti(hmd, OHMD_SCREEN_VERTICAL_RESOLUTION, &ival);
	ohmd_device_getf(hmd, OHMD_SCREEN_HORIZONTAL_SIZE, &fval);
	ohmd_device_getf(hmd, OHMD_SCREEN_VERTICAL_SIZE, &fval);
	ohmd_device_getf(hmd, OHMD_LENS_HORIZONTAL_SEPARATION, &fval);
	ohmd_device_getf(hmd, OHMD_LENS_VERTICAL_POSITION, &fval);
	ohmd_device_getf(hmd, OHMD_LEFT_EYE_FOV, &fval);
	ohmd_device_getf(hmd, OHMD_LEFT_EYE_ASPECT_RATIO, &fval);
    */
}

GHOST_OpenHMDManager::~GHOST_OpenHMDManager()
{
    if (m_available) {
        ohmd_ctx_destroy(m_context);
        m_context = NULL;
        m_device = NULL;
        m_available = false;
    }
}

bool GHOST_OpenHMDManager::processEvents()
{
	if (m_available) {
        ohmd_ctx_update(m_context);

        GHOST_TUns64 now = m_system.getMilliSeconds();

        GHOST_EventOpenHMD *event = new GHOST_EventOpenHMD(now, m_system.getWindowManager()->getActiveWindow());
        GHOST_TEventOpenHMDData* data = (GHOST_TEventOpenHMDData*) event->getData();

        ohmd_device_getf(m_device, OHMD_ROTATION_QUAT, data->orientation);

        m_system.pushEvent(event);
        return true;
	}
	else {
        return false;
	}
}
