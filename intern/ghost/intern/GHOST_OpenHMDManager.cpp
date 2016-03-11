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

            printf("Found %i OpenHMD devices\n", num_devices);
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
		GHOST_IWindow *window = m_system.getWindowManager()->getActiveWindow();

		if (!window)
			return false;

		ohmd_ctx_update(m_context);

        GHOST_TUns64 now = m_system.getMilliSeconds();

        GHOST_EventOpenHMD *event = new GHOST_EventOpenHMD(now, window);
        GHOST_TEventOpenHMDData* data = (GHOST_TEventOpenHMDData*) event->getData();
        float quat[4];

        ohmd_device_getf(m_device, OHMD_ROTATION_QUAT, quat);
        data->orientation[0] = quat[3];
        data->orientation[1] = quat[0];
        data->orientation[2] = quat[1];
        data->orientation[3] = quat[2];

//        printf("sending openhmd event with data x y z w %f %f %f %f at time %llu\n",
//               data->orientation[0],
//               data->orientation[1],
//               data->orientation[2],
//               data->orientation[3],
//               now);


        m_system.pushEvent(event);
        return true;
	}
	else {
        return false;
	}
}

bool GHOST_OpenHMDManager::available() const
{
    return m_available;
}

bool GHOST_OpenHMDManager::setDevice(const char* requested_vendor_name, const char* requested_device_name)
{
    if (!m_available) {
        return false;
    }

    int num_devices = ohmd_ctx_probe(m_context);
    for (int i = 0; i < num_devices; ++i) {
        const char* device_name = ohmd_list_gets(m_context, i, OHMD_PRODUCT);
        const char* vendor_name = ohmd_list_gets(m_context, i, OHMD_VENDOR);

        if (strcmp(device_name, requested_device_name) == 0 && strcmp(vendor_name, requested_vendor_name) == 0) {
            //can't fail
            m_device = ohmd_list_open_device(m_context, i);
            return true;
        }
    }

    //couldn't find a device by that name and vendor, the old device was preserved.
    return false;
}
