/*
 * Copyright 2016 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License
 */

#ifdef _MSC_VER
#  define snprintf _snprintf
#  define popen _popen
#  define pclose _pclose
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "udew.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define VC_EXTRALEAN
#  include <windows.h>

/* Utility macros. */

typedef HMODULE DynamicLibrary;

#  define dynamic_library_open(path)         LoadLibrary(path)
#  define dynamic_library_close(lib)         FreeLibrary(lib)
#  define dynamic_library_find(lib, symbol)  GetProcAddress(lib, symbol)
#else
#  include <dlfcn.h>

typedef void* DynamicLibrary;

#  define dynamic_library_open(path)         dlopen(path, RTLD_NOW)
#  define dynamic_library_close(lib)         dlclose(lib)
#  define dynamic_library_find(lib, symbol)  dlsym(lib, symbol)
#endif

#define GLUE(a, b) a ## b

#define UDEV_LIBRARY_FIND_CHECKED(name)         GLUE(_, name) = (t##name *)dynamic_library_find(lib, #name);         assert(GLUE(_, name));

#define UDEV_LIBRARY_FIND(name)         GLUE(_, name) = (t##name *)dynamic_library_find(lib, #name);

static DynamicLibrary lib;

tudev_ref *_udev_ref;
tudev_unref *_udev_unref;
tudev_new *_udev_new;
tudev_set_log_fn *_udev_set_log_fn;
tudev_get_log_priority *_udev_get_log_priority;
tudev_set_log_priority *_udev_set_log_priority;
tudev_get_userdata *_udev_get_userdata;
tudev_set_userdata *_udev_set_userdata;
tudev_list_entry_get_next *_udev_list_entry_get_next;
tudev_list_entry_get_by_name *_udev_list_entry_get_by_name;
tudev_list_entry_get_name *_udev_list_entry_get_name;
tudev_list_entry_get_value *_udev_list_entry_get_value;
tudev_device_ref *_udev_device_ref;
tudev_device_unref *_udev_device_unref;
tudev_device_get_udev *_udev_device_get_udev;
tudev_device_new_from_syspath *_udev_device_new_from_syspath;
tudev_device_new_from_devnum *_udev_device_new_from_devnum;
tudev_device_new_from_subsystem_sysname *_udev_device_new_from_subsystem_sysname;
tudev_device_new_from_device_id *_udev_device_new_from_device_id;
tudev_device_new_from_environment *_udev_device_new_from_environment;
tudev_device_get_parent *_udev_device_get_parent;
tudev_device_get_parent_with_subsystem_devtype *_udev_device_get_parent_with_subsystem_devtype;
tudev_device_get_devpath *_udev_device_get_devpath;
tudev_device_get_subsystem *_udev_device_get_subsystem;
tudev_device_get_devtype *_udev_device_get_devtype;
tudev_device_get_syspath *_udev_device_get_syspath;
tudev_device_get_sysname *_udev_device_get_sysname;
tudev_device_get_sysnum *_udev_device_get_sysnum;
tudev_device_get_devnode *_udev_device_get_devnode;
tudev_device_get_is_initialized *_udev_device_get_is_initialized;
tudev_device_get_devlinks_list_entry *_udev_device_get_devlinks_list_entry;
tudev_device_get_properties_list_entry *_udev_device_get_properties_list_entry;
tudev_device_get_tags_list_entry *_udev_device_get_tags_list_entry;
tudev_device_get_sysattr_list_entry *_udev_device_get_sysattr_list_entry;
tudev_device_get_property_value *_udev_device_get_property_value;
tudev_device_get_driver *_udev_device_get_driver;
tudev_device_get_devnum *_udev_device_get_devnum;
tudev_device_get_action *_udev_device_get_action;
tudev_device_get_seqnum *_udev_device_get_seqnum;
tudev_device_get_usec_since_initialized *_udev_device_get_usec_since_initialized;
tudev_device_get_sysattr_value *_udev_device_get_sysattr_value;
tudev_device_set_sysattr_value *_udev_device_set_sysattr_value;
tudev_device_has_tag *_udev_device_has_tag;
tudev_monitor_ref *_udev_monitor_ref;
tudev_monitor_unref *_udev_monitor_unref;
tudev_monitor_get_udev *_udev_monitor_get_udev;
tudev_monitor_new_from_netlink *_udev_monitor_new_from_netlink;
tudev_monitor_enable_receiving *_udev_monitor_enable_receiving;
tudev_monitor_set_receive_buffer_size *_udev_monitor_set_receive_buffer_size;
tudev_monitor_get_fd *_udev_monitor_get_fd;
tudev_monitor_receive_device *_udev_monitor_receive_device;
tudev_monitor_filter_add_match_subsystem_devtype *_udev_monitor_filter_add_match_subsystem_devtype;
tudev_monitor_filter_add_match_tag *_udev_monitor_filter_add_match_tag;
tudev_monitor_filter_update *_udev_monitor_filter_update;
tudev_monitor_filter_remove *_udev_monitor_filter_remove;
tudev_enumerate_ref *_udev_enumerate_ref;
tudev_enumerate_unref *_udev_enumerate_unref;
tudev_enumerate_get_udev *_udev_enumerate_get_udev;
tudev_enumerate_new *_udev_enumerate_new;
tudev_enumerate_add_match_subsystem *_udev_enumerate_add_match_subsystem;
tudev_enumerate_add_nomatch_subsystem *_udev_enumerate_add_nomatch_subsystem;
tudev_enumerate_add_match_sysattr *_udev_enumerate_add_match_sysattr;
tudev_enumerate_add_nomatch_sysattr *_udev_enumerate_add_nomatch_sysattr;
tudev_enumerate_add_match_property *_udev_enumerate_add_match_property;
tudev_enumerate_add_match_sysname *_udev_enumerate_add_match_sysname;
tudev_enumerate_add_match_tag *_udev_enumerate_add_match_tag;
tudev_enumerate_add_match_parent *_udev_enumerate_add_match_parent;
tudev_enumerate_add_match_is_initialized *_udev_enumerate_add_match_is_initialized;
tudev_enumerate_add_syspath *_udev_enumerate_add_syspath;
tudev_enumerate_scan_devices *_udev_enumerate_scan_devices;
tudev_enumerate_scan_subsystems *_udev_enumerate_scan_subsystems;
tudev_enumerate_get_list_entry *_udev_enumerate_get_list_entry;
tudev_queue_ref *_udev_queue_ref;
tudev_queue_unref *_udev_queue_unref;
tudev_queue_get_udev *_udev_queue_get_udev;
tudev_queue_new *_udev_queue_new;
tudev_queue_get_kernel_seqnum *_udev_queue_get_kernel_seqnum;
tudev_queue_get_udev_seqnum *_udev_queue_get_udev_seqnum;
tudev_queue_get_udev_is_active *_udev_queue_get_udev_is_active;
tudev_queue_get_queue_is_empty *_udev_queue_get_queue_is_empty;
tudev_queue_get_seqnum_is_finished *_udev_queue_get_seqnum_is_finished;
tudev_queue_get_seqnum_sequence_is_finished *_udev_queue_get_seqnum_sequence_is_finished;
tudev_queue_get_fd *_udev_queue_get_fd;
tudev_queue_flush *_udev_queue_flush;
tudev_queue_get_queued_list_entry *_udev_queue_get_queued_list_entry;
tudev_hwdb_new *_udev_hwdb_new;
tudev_hwdb_ref *_udev_hwdb_ref;
tudev_hwdb_unref *_udev_hwdb_unref;
tudev_hwdb_get_properties_list_entry *_udev_hwdb_get_properties_list_entry;
tudev_util_encode_string *_udev_util_encode_string;

static DynamicLibrary dynamic_library_open_find(const char **paths) {
  int i = 0;
  while (paths[i] != NULL) {
      DynamicLibrary lib = dynamic_library_open(paths[i]);
      if (lib != NULL) {
        return lib;
      }
      ++i;
  }
  return NULL;
}

static void udewExit(void) {
  if(lib != NULL) {
    /*  Ignore errors. */
    dynamic_library_close(lib);
    lib = NULL;
  }
}

/* Implementation function. */
int udewInit(void) {
  /* Library paths. */
#ifdef _WIN32
  /* Expected in c:/windows/system or similar, no path needed. */
  const char *paths[] = {"udev.dll", NULL};
#elif defined(__APPLE__)
  /* Default installation path. */
  const char *paths[] = {"libudev.dylib", NULL};
#else
  const char *paths[] = {"libudev.so",
                         "libudev.so.0",
                         "libudev.so.1",
                         "libudev.so.2",
                         NULL};
#endif
  static int initialized = 0;
  static int result = 0;
  int error;

  if (initialized) {
    return result;
  }

  initialized = 1;

  error = atexit(udewExit);
  if (error) {
    result = UDEW_ERROR_ATEXIT_FAILED;
    return result;
  }

  /* Load library. */
  lib = dynamic_library_open_find(paths);

  if (lib == NULL) {
    result = UDEW_ERROR_OPEN_FAILED;
    return result;
  }

  UDEV_LIBRARY_FIND(udev_ref);
  UDEV_LIBRARY_FIND(udev_unref);
  UDEV_LIBRARY_FIND(udev_new);
  UDEV_LIBRARY_FIND(udev_set_log_fn);
  UDEV_LIBRARY_FIND(udev_get_log_priority);
  UDEV_LIBRARY_FIND(udev_set_log_priority);
  UDEV_LIBRARY_FIND(udev_get_userdata);
  UDEV_LIBRARY_FIND(udev_set_userdata);
  UDEV_LIBRARY_FIND(udev_list_entry_get_next);
  UDEV_LIBRARY_FIND(udev_list_entry_get_by_name);
  UDEV_LIBRARY_FIND(udev_list_entry_get_name);
  UDEV_LIBRARY_FIND(udev_list_entry_get_value);
  UDEV_LIBRARY_FIND(udev_device_ref);
  UDEV_LIBRARY_FIND(udev_device_unref);
  UDEV_LIBRARY_FIND(udev_device_get_udev);
  UDEV_LIBRARY_FIND(udev_device_new_from_syspath);
  UDEV_LIBRARY_FIND(udev_device_new_from_devnum);
  UDEV_LIBRARY_FIND(udev_device_new_from_subsystem_sysname);
  UDEV_LIBRARY_FIND(udev_device_new_from_device_id);
  UDEV_LIBRARY_FIND(udev_device_new_from_environment);
  UDEV_LIBRARY_FIND(udev_device_get_parent);
  UDEV_LIBRARY_FIND(udev_device_get_parent_with_subsystem_devtype);
  UDEV_LIBRARY_FIND(udev_device_get_devpath);
  UDEV_LIBRARY_FIND(udev_device_get_subsystem);
  UDEV_LIBRARY_FIND(udev_device_get_devtype);
  UDEV_LIBRARY_FIND(udev_device_get_syspath);
  UDEV_LIBRARY_FIND(udev_device_get_sysname);
  UDEV_LIBRARY_FIND(udev_device_get_sysnum);
  UDEV_LIBRARY_FIND(udev_device_get_devnode);
  UDEV_LIBRARY_FIND(udev_device_get_is_initialized);
  UDEV_LIBRARY_FIND(udev_device_get_devlinks_list_entry);
  UDEV_LIBRARY_FIND(udev_device_get_properties_list_entry);
  UDEV_LIBRARY_FIND(udev_device_get_tags_list_entry);
  UDEV_LIBRARY_FIND(udev_device_get_sysattr_list_entry);
  UDEV_LIBRARY_FIND(udev_device_get_property_value);
  UDEV_LIBRARY_FIND(udev_device_get_driver);
  UDEV_LIBRARY_FIND(udev_device_get_devnum);
  UDEV_LIBRARY_FIND(udev_device_get_action);
  UDEV_LIBRARY_FIND(udev_device_get_seqnum);
  UDEV_LIBRARY_FIND(udev_device_get_usec_since_initialized);
  UDEV_LIBRARY_FIND(udev_device_get_sysattr_value);
  UDEV_LIBRARY_FIND(udev_device_set_sysattr_value);
  UDEV_LIBRARY_FIND(udev_device_has_tag);
  UDEV_LIBRARY_FIND(udev_monitor_ref);
  UDEV_LIBRARY_FIND(udev_monitor_unref);
  UDEV_LIBRARY_FIND(udev_monitor_get_udev);
  UDEV_LIBRARY_FIND(udev_monitor_new_from_netlink);
  UDEV_LIBRARY_FIND(udev_monitor_enable_receiving);
  UDEV_LIBRARY_FIND(udev_monitor_set_receive_buffer_size);
  UDEV_LIBRARY_FIND(udev_monitor_get_fd);
  UDEV_LIBRARY_FIND(udev_monitor_receive_device);
  UDEV_LIBRARY_FIND(udev_monitor_filter_add_match_subsystem_devtype);
  UDEV_LIBRARY_FIND(udev_monitor_filter_add_match_tag);
  UDEV_LIBRARY_FIND(udev_monitor_filter_update);
  UDEV_LIBRARY_FIND(udev_monitor_filter_remove);
  UDEV_LIBRARY_FIND(udev_enumerate_ref);
  UDEV_LIBRARY_FIND(udev_enumerate_unref);
  UDEV_LIBRARY_FIND(udev_enumerate_get_udev);
  UDEV_LIBRARY_FIND(udev_enumerate_new);
  UDEV_LIBRARY_FIND(udev_enumerate_add_match_subsystem);
  UDEV_LIBRARY_FIND(udev_enumerate_add_nomatch_subsystem);
  UDEV_LIBRARY_FIND(udev_enumerate_add_match_sysattr);
  UDEV_LIBRARY_FIND(udev_enumerate_add_nomatch_sysattr);
  UDEV_LIBRARY_FIND(udev_enumerate_add_match_property);
  UDEV_LIBRARY_FIND(udev_enumerate_add_match_sysname);
  UDEV_LIBRARY_FIND(udev_enumerate_add_match_tag);
  UDEV_LIBRARY_FIND(udev_enumerate_add_match_parent);
  UDEV_LIBRARY_FIND(udev_enumerate_add_match_is_initialized);
  UDEV_LIBRARY_FIND(udev_enumerate_add_syspath);
  UDEV_LIBRARY_FIND(udev_enumerate_scan_devices);
  UDEV_LIBRARY_FIND(udev_enumerate_scan_subsystems);
  UDEV_LIBRARY_FIND(udev_enumerate_get_list_entry);
  UDEV_LIBRARY_FIND(udev_queue_ref);
  UDEV_LIBRARY_FIND(udev_queue_unref);
  UDEV_LIBRARY_FIND(udev_queue_get_udev);
  UDEV_LIBRARY_FIND(udev_queue_new);
  UDEV_LIBRARY_FIND(udev_queue_get_kernel_seqnum);
  UDEV_LIBRARY_FIND(udev_queue_get_udev_seqnum);
  UDEV_LIBRARY_FIND(udev_queue_get_udev_is_active);
  UDEV_LIBRARY_FIND(udev_queue_get_queue_is_empty);
  UDEV_LIBRARY_FIND(udev_queue_get_seqnum_is_finished);
  UDEV_LIBRARY_FIND(udev_queue_get_seqnum_sequence_is_finished);
  UDEV_LIBRARY_FIND(udev_queue_get_fd);
  UDEV_LIBRARY_FIND(udev_queue_flush);
  UDEV_LIBRARY_FIND(udev_queue_get_queued_list_entry);
  UDEV_LIBRARY_FIND(udev_hwdb_new);
  UDEV_LIBRARY_FIND(udev_hwdb_ref);
  UDEV_LIBRARY_FIND(udev_hwdb_unref);
  UDEV_LIBRARY_FIND(udev_hwdb_get_properties_list_entry);
  UDEV_LIBRARY_FIND(udev_util_encode_string);

  result = UDEW_SUCCESS;

  return result;
}

struct udev *udev_ref(struct udev *udev) {
  return _udev_ref(udev);
}

struct udev *udev_unref(struct udev *udev) {
  return _udev_unref(udev);
}

struct udev *udev_new(void) {
  return _udev_new();
}

int udev_get_log_priority(struct udev *udev) {
  return _udev_get_log_priority(udev);
}

void udev_set_log_priority(struct udev *udev, int priority) {
  return _udev_set_log_priority(udev, priority);
}

void *udev_get_userdata(struct udev *udev) {
  return _udev_get_userdata(udev);
}

void udev_set_userdata(struct udev *udev, void *userdata) {
  return _udev_set_userdata(udev, userdata);
}

struct udev_list_entry *udev_list_entry_get_next(struct udev_list_entry *list_entry) {
  return _udev_list_entry_get_next(list_entry);
}

struct udev_list_entry *udev_list_entry_get_by_name(struct udev_list_entry *list_entry, const char *name) {
  return _udev_list_entry_get_by_name(list_entry, name);
}

const char *udev_list_entry_get_name(struct udev_list_entry *list_entry) {
  return _udev_list_entry_get_name(list_entry);
}

const char *udev_list_entry_get_value(struct udev_list_entry *list_entry) {
  return _udev_list_entry_get_value(list_entry);
}

struct udev_device *udev_device_ref(struct udev_device *udev_device) {
  return _udev_device_ref(udev_device);
}

struct udev_device *udev_device_unref(struct udev_device *udev_device) {
  return _udev_device_unref(udev_device);
}

struct udev *udev_device_get_udev(struct udev_device *udev_device) {
  return _udev_device_get_udev(udev_device);
}

struct udev_device *udev_device_new_from_syspath(struct udev *udev, const char *syspath) {
  return _udev_device_new_from_syspath(udev, syspath);
}

struct udev_device *udev_device_new_from_devnum(struct udev *udev, char type, dev_t devnum) {
  return _udev_device_new_from_devnum(udev, type, devnum);
}

struct udev_device *udev_device_new_from_subsystem_sysname(struct udev *udev, const char *subsystem, const char *sysname) {
  return _udev_device_new_from_subsystem_sysname(udev, subsystem, sysname);
}

struct udev_device *udev_device_new_from_device_id(struct udev *udev, const char *id) {
  return _udev_device_new_from_device_id(udev, id);
}

struct udev_device *udev_device_new_from_environment(struct udev *udev) {
  return _udev_device_new_from_environment(udev);
}

struct udev_device *udev_device_get_parent(struct udev_device *udev_device) {
  return _udev_device_get_parent(udev_device);
}

struct udev_device *udev_device_get_parent_with_subsystem_devtype(struct udev_device *udev_device, const char *subsystem, const char *devtype) {
  return _udev_device_get_parent_with_subsystem_devtype(udev_device, subsystem, devtype);
}

const char *udev_device_get_devpath(struct udev_device *udev_device) {
  return _udev_device_get_devpath(udev_device);
}

const char *udev_device_get_subsystem(struct udev_device *udev_device) {
  return _udev_device_get_subsystem(udev_device);
}

const char *udev_device_get_devtype(struct udev_device *udev_device) {
  return _udev_device_get_devtype(udev_device);
}

const char *udev_device_get_syspath(struct udev_device *udev_device) {
  return _udev_device_get_syspath(udev_device);
}

const char *udev_device_get_sysname(struct udev_device *udev_device) {
  return _udev_device_get_sysname(udev_device);
}

const char *udev_device_get_sysnum(struct udev_device *udev_device) {
  return _udev_device_get_sysnum(udev_device);
}

const char *udev_device_get_devnode(struct udev_device *udev_device) {
  return _udev_device_get_devnode(udev_device);
}

int udev_device_get_is_initialized(struct udev_device *udev_device) {
  return _udev_device_get_is_initialized(udev_device);
}

struct udev_list_entry *udev_device_get_devlinks_list_entry(struct udev_device *udev_device) {
  return _udev_device_get_devlinks_list_entry(udev_device);
}

struct udev_list_entry *udev_device_get_properties_list_entry(struct udev_device *udev_device) {
  return _udev_device_get_properties_list_entry(udev_device);
}

struct udev_list_entry *udev_device_get_tags_list_entry(struct udev_device *udev_device) {
  return _udev_device_get_tags_list_entry(udev_device);
}

struct udev_list_entry *udev_device_get_sysattr_list_entry(struct udev_device *udev_device) {
  return _udev_device_get_sysattr_list_entry(udev_device);
}

const char *udev_device_get_property_value(struct udev_device *udev_device, const char *key) {
  return _udev_device_get_property_value(udev_device, key);
}

const char *udev_device_get_driver(struct udev_device *udev_device) {
  return _udev_device_get_driver(udev_device);
}

dev_t udev_device_get_devnum(struct udev_device *udev_device) {
  return _udev_device_get_devnum(udev_device);
}

const char *udev_device_get_action(struct udev_device *udev_device) {
  return _udev_device_get_action(udev_device);
}

unsigned long long int udev_device_get_seqnum(struct udev_device *udev_device) {
  return _udev_device_get_seqnum(udev_device);
}

unsigned long long int udev_device_get_usec_since_initialized(struct udev_device *udev_device) {
  return _udev_device_get_usec_since_initialized(udev_device);
}

const char *udev_device_get_sysattr_value(struct udev_device *udev_device, const char *sysattr) {
  return _udev_device_get_sysattr_value(udev_device, sysattr);
}

int udev_device_set_sysattr_value(struct udev_device *udev_device, const char *sysattr, char *value) {
  return _udev_device_set_sysattr_value(udev_device, sysattr, value);
}

int udev_device_has_tag(struct udev_device *udev_device, const char *tag) {
  return _udev_device_has_tag(udev_device, tag);
}

struct udev_monitor *udev_monitor_ref(struct udev_monitor *udev_monitor) {
  return _udev_monitor_ref(udev_monitor);
}

struct udev_monitor *udev_monitor_unref(struct udev_monitor *udev_monitor) {
  return _udev_monitor_unref(udev_monitor);
}

struct udev *udev_monitor_get_udev(struct udev_monitor *udev_monitor) {
  return _udev_monitor_get_udev(udev_monitor);
}

struct udev_monitor *udev_monitor_new_from_netlink(struct udev *udev, const char *name) {
  return _udev_monitor_new_from_netlink(udev, name);
}

int udev_monitor_enable_receiving(struct udev_monitor *udev_monitor) {
  return _udev_monitor_enable_receiving(udev_monitor);
}

int udev_monitor_set_receive_buffer_size(struct udev_monitor *udev_monitor, int size) {
  return _udev_monitor_set_receive_buffer_size(udev_monitor, size);
}

int udev_monitor_get_fd(struct udev_monitor *udev_monitor) {
  return _udev_monitor_get_fd(udev_monitor);
}

struct udev_device *udev_monitor_receive_device(struct udev_monitor *udev_monitor) {
  return _udev_monitor_receive_device(udev_monitor);
}

int udev_monitor_filter_add_match_tag(struct udev_monitor *udev_monitor, const char *tag) {
  return _udev_monitor_filter_add_match_tag(udev_monitor, tag);
}

int udev_monitor_filter_update(struct udev_monitor *udev_monitor) {
  return _udev_monitor_filter_update(udev_monitor);
}

int udev_monitor_filter_remove(struct udev_monitor *udev_monitor) {
  return _udev_monitor_filter_remove(udev_monitor);
}

struct udev_enumerate *udev_enumerate_ref(struct udev_enumerate *udev_enumerate) {
  return _udev_enumerate_ref(udev_enumerate);
}

struct udev_enumerate *udev_enumerate_unref(struct udev_enumerate *udev_enumerate) {
  return _udev_enumerate_unref(udev_enumerate);
}

struct udev *udev_enumerate_get_udev(struct udev_enumerate *udev_enumerate) {
  return _udev_enumerate_get_udev(udev_enumerate);
}

struct udev_enumerate *udev_enumerate_new(struct udev *udev) {
  return _udev_enumerate_new(udev);
}

int udev_enumerate_add_match_subsystem(struct udev_enumerate *udev_enumerate, const char *subsystem) {
  return _udev_enumerate_add_match_subsystem(udev_enumerate, subsystem);
}

int udev_enumerate_add_nomatch_subsystem(struct udev_enumerate *udev_enumerate, const char *subsystem) {
  return _udev_enumerate_add_nomatch_subsystem(udev_enumerate, subsystem);
}

int udev_enumerate_add_match_sysattr(struct udev_enumerate *udev_enumerate, const char *sysattr, const char *value) {
  return _udev_enumerate_add_match_sysattr(udev_enumerate, sysattr, value);
}

int udev_enumerate_add_nomatch_sysattr(struct udev_enumerate *udev_enumerate, const char *sysattr, const char *value) {
  return _udev_enumerate_add_nomatch_sysattr(udev_enumerate, sysattr, value);
}

int udev_enumerate_add_match_property(struct udev_enumerate *udev_enumerate, const char *property, const char *value) {
  return _udev_enumerate_add_match_property(udev_enumerate, property, value);
}

int udev_enumerate_add_match_sysname(struct udev_enumerate *udev_enumerate, const char *sysname) {
  return _udev_enumerate_add_match_sysname(udev_enumerate, sysname);
}

int udev_enumerate_add_match_tag(struct udev_enumerate *udev_enumerate, const char *tag) {
  return _udev_enumerate_add_match_tag(udev_enumerate, tag);
}

int udev_enumerate_add_match_parent(struct udev_enumerate *udev_enumerate, struct udev_device *parent) {
  return _udev_enumerate_add_match_parent(udev_enumerate, parent);
}

int udev_enumerate_add_match_is_initialized(struct udev_enumerate *udev_enumerate) {
  return _udev_enumerate_add_match_is_initialized(udev_enumerate);
}

int udev_enumerate_add_syspath(struct udev_enumerate *udev_enumerate, const char *syspath) {
  return _udev_enumerate_add_syspath(udev_enumerate, syspath);
}

int udev_enumerate_scan_devices(struct udev_enumerate *udev_enumerate) {
  return _udev_enumerate_scan_devices(udev_enumerate);
}

int udev_enumerate_scan_subsystems(struct udev_enumerate *udev_enumerate) {
  return _udev_enumerate_scan_subsystems(udev_enumerate);
}

struct udev_list_entry *udev_enumerate_get_list_entry(struct udev_enumerate *udev_enumerate) {
  return _udev_enumerate_get_list_entry(udev_enumerate);
}

struct udev_queue *udev_queue_ref(struct udev_queue *udev_queue) {
  return _udev_queue_ref(udev_queue);
}

struct udev_queue *udev_queue_unref(struct udev_queue *udev_queue) {
  return _udev_queue_unref(udev_queue);
}

struct udev *udev_queue_get_udev(struct udev_queue *udev_queue) {
  return _udev_queue_get_udev(udev_queue);
}

struct udev_queue *udev_queue_new(struct udev *udev) {
  return _udev_queue_new(udev);
}

unsigned long long int udev_queue_get_kernel_seqnum(struct udev_queue *udev_queue) {
  return _udev_queue_get_kernel_seqnum(udev_queue);
}

unsigned long long int udev_queue_get_udev_seqnum(struct udev_queue *udev_queue) {
  return _udev_queue_get_udev_seqnum(udev_queue);
}

int udev_queue_get_udev_is_active(struct udev_queue *udev_queue) {
  return _udev_queue_get_udev_is_active(udev_queue);
}

int udev_queue_get_queue_is_empty(struct udev_queue *udev_queue) {
  return _udev_queue_get_queue_is_empty(udev_queue);
}

int udev_queue_get_seqnum_is_finished(struct udev_queue *udev_queue, unsigned long long int seqnum) {
  return _udev_queue_get_seqnum_is_finished(udev_queue, seqnum);
}

int udev_queue_get_fd(struct udev_queue *udev_queue) {
  return _udev_queue_get_fd(udev_queue);
}

int udev_queue_flush(struct udev_queue *udev_queue) {
  return _udev_queue_flush(udev_queue);
}

struct udev_list_entry *udev_queue_get_queued_list_entry(struct udev_queue *udev_queue) {
  return _udev_queue_get_queued_list_entry(udev_queue);
}

struct udev_hwdb *udev_hwdb_new(struct udev *udev) {
  return _udev_hwdb_new(udev);
}

struct udev_hwdb *udev_hwdb_ref(struct udev_hwdb *hwdb) {
  return _udev_hwdb_ref(hwdb);
}

struct udev_hwdb *udev_hwdb_unref(struct udev_hwdb *hwdb) {
  return _udev_hwdb_unref(hwdb);
}

struct udev_list_entry *udev_hwdb_get_properties_list_entry(struct udev_hwdb *hwdb, const char *modalias, unsigned int flags) {
  return _udev_hwdb_get_properties_list_entry(hwdb, modalias, flags);
}

int udev_util_encode_string(const char *str, char *str_enc, size_t len) {
  return _udev_util_encode_string(str, str_enc, len);
}

