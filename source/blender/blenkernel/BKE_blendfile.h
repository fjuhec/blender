/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 */
#ifndef __BKE_BLENDFILE_H__
#define __BKE_BLENDFILE_H__

/** \file BKE_blendfile.h
 *  \ingroup bke
 *  \since March 2001
 *  \author nzc
 *  \brief Blender util stuff
 */

#ifdef __cplusplus
extern "C" {
#endif

struct bContext;
struct ID;
struct Main;
struct MemFile;
struct ReportList;

int BKE_blendfile_read(struct bContext *C, const char *filepath, struct ReportList *reports);

#define BKE_READ_FILE_FAIL              0 /* no load */
#define BKE_READ_FILE_OK                1 /* OK */
#define BKE_READ_FILE_OK_USERPREFS      2 /* OK, and with new user settings */

bool BKE_blendfile_read_from_memory(
        struct bContext *C, const void *filebuf,
        int filelength, struct ReportList *reports, bool update_defaults);
bool BKE_blendfile_read_from_memfile(
        struct bContext *C, struct MemFile *memfile,
        struct ReportList *reports);

int BKE_blendfile_read_userdef(const char *filepath, struct ReportList *reports);
int BKE_blendfile_write_userdef(const char *filepath, struct ReportList *reports);


/* partial blend file writing */
void BKE_blendfile_write_partial_tag_ID(struct ID *id, bool set);
void BKE_blendfile_write_partial_begin(struct Main *bmain_src);
bool BKE_blendfile_write_partial(
        struct Main *bmain_src, const char *filepath, const int write_flags, struct ReportList *reports);
void BKE_blendfile_write_partial_end(struct Main *bmain_src);

#ifdef __cplusplus
}
#endif

#endif  /* __BKE_BLENDFILE_H__ */
