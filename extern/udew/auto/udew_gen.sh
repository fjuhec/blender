#!/bin/bash

#HEADER="/usr/include/libudev.h"
HEADER="libudev.h"
SCRIPT=`realpath -s $0`
DIR=`dirname $SCRIPT`
DIR=`dirname $DIR`
H="$DIR/include/udew.h"
C=$DIR/src/udew.c
STUBS=true

mkdir -p $DIR/include
mkdir -p $DIR/src

rm -rf $DIR/include/udew.h
rm -rf $DIR/src/udew.c

echo "Generating udew headers..."

append() {
  f=`echo "$1" | sed ':a;N;$!ba;s/\n/\\\\n/g'`
  line_num=`grep -n '#ifdef __cplusplus' $H | cut -d : -f 1 | tail -n 1`
  gawk -i inplace "NR==$line_num {\$0=\"$f\n\n#ifdef __cplusplus\"} 1" $H
}

cat $HEADER \
    | sed -r 's/^((const )?[a-z0-9_]+( [a-z0-9_]+)* \*?)([a-z0-9_]+)\(/typedef \1t\4(/i' \
    > $H

if $STUBS; then
  fp="_";
fi

f=`grep -E 'typedef ((const )?[a-z0-9_]+( [a-z0-9_]+)* \*?)t([a-z0-9_]+)\(' $H \
   | sed -r "s/((const )?[a-z0-9_]+( [a-z0-9_]+)* \*?)t([a-z0-9_]+)\(.*/extern t\4 *$fp\4;/i"`
append "$f"

append "enum {\n\
  UDEW_SUCCESS = 0,\n\
  UDEW_ERROR_OPEN_FAILED = -1,\n\
  UDEW_ERROR_ATEXIT_FAILED = -2,\n\
};\n\
\n\
int udewInit(void);"

if $STUBS; then
  decl=`cat $HEADER \
          | grep -E '^((const )?[a-z0-9_]+( [a-z0-9_]+)* \*?)([a-z0-9_]+)\(.*\);'`
  append "$decl"
fi

sed -i 's/_LIBUDEV_H_/__UDEW_H__/g' $H
sed -i 's/ __attribute__ ((deprecated))//g' $H

line_num=`grep -n '\*\*\*/' $H | cut -d : -f 1`
for x in `seq $line_num`; do
  sed -i '1d' $H
done

mv $H $H.tmp
cat << EOF > $H
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
EOF

cat $H.tmp >> $H

rm $H.tmp

echo "Generating udew source..."

cat << EOF > $C
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

#define UDEV_LIBRARY_FIND_CHECKED(name) \
        GLUE(${fp}, name) = (t##name *)dynamic_library_find(lib, #name); \
        assert(GLUE(${fp}, name));

#define UDEV_LIBRARY_FIND(name) \
        GLUE(${fp}, name) = (t##name *)dynamic_library_find(lib, #name);

static DynamicLibrary lib;

EOF

content=`grep --no-filename -ER "extern t" $DIR/include/udew.h`

echo "$content" | sed -r "s/extern t([a-z0-9_]+).*/t\1 *$fp\1;/gi" >> $C

cat << EOF >> $C

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

EOF

echo "$content" | sed -r 's/extern t([a-z0-9_]+).*/  UDEV_LIBRARY_FIND(\1);/gi' >> $C

cat << EOF >> $C

  result = UDEW_SUCCESS;

  return result;
}
EOF

if $STUBS; then
  echo "" >> $C
  echo "$decl" | while read l; do
    h=`echo "$l" | sed -r 's/;//' | sed 's/ __attribute__ ((deprecated))//'`
    echo "$h {" >> $C

    f=`echo $h | sed -r 's/^((const )?[a-z0-9_]+( [a-z0-9_]+)* \*?)([a-z0-9_]+)\(.*/\4/i'`
    args=`echo $h | sed -r 's/^((const )?[a-z0-9_]+( [a-z0-9_]+)* \*?)([a-z0-9_]+)\((.*)\).*?/\5/i'`
    args=`echo $args | python -c "import sys; l=sys.stdin.readline(); l=l.strip(); print(', '.join([x.split(' ')[-1].replace('*', '') for x in l.split(',')]))"`
    if [ "$args" = "void" ]; then
      args=""
    fi
    echo "  return $fp$f($args);" >> $C
    echo -e "}\n" >> $C
  done
fi
