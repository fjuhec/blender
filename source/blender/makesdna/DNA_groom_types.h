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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file DNA_groom_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_GROOM_TYPES_H__
#define __DNA_GROOM_TYPES_H__

#include "DNA_defs.h"
#include "DNA_listBase.h"
#include "DNA_ID.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cross-section of a bundle */
typedef struct GroomBundleSection {
	struct GroomBundleSection *next, *prev; /* Pointers for ListBase element */
	
	float center[3];                        /* Center point */
	float normal[3];                        /* Normal direction of the section plane */
} GroomBundleSection;

/* Bundle of hair strands following the same curve path */
typedef struct GroomBundle {
	struct GroomBundle *next, *prev;    /* Pointers for ListBase element */
	
	ListBase sections;                  /* List of GroomBundleSection */
} GroomBundle;

/* Editable groom data */
typedef struct EditGroom {
	ListBase bundles;       /* List of GroomBundle */
} EditGroom;

/* Groom curves for creating hair styles */
typedef struct Groom {
	ID id;                  /* Groom data is a datablock */
	struct AnimData *adt;   /* Animation data - for animating settings */
	
	struct BoundBox *bb;
	
	ListBase bundles;       /* List of GroomBundle */
	
	EditGroom *edit_groom;
} Groom;

#ifdef __cplusplus
}
#endif

#endif /* __DNA_GROOM_TYPES_H__ */
