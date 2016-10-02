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
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_view3d/view3d_widgets.c
 *  \ingroup spview3d
 */


#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_camera.h"
#include "BKE_context.h"
#include "BKE_object.h"

#include "DNA_armature_types.h"
#include "DNA_camera_types.h"
#include "DNA_lamp_types.h"
#include "DNA_object_types.h"
#include "DNA_object_force.h"
#include "DNA_manipulator_types.h"

#include "ED_armature.h"
#include "ED_screen.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

#include "view3d_intern.h"  /* own include */

typedef struct CameraWidgetGroup {
	wmManipulator *dop_dist,
	         *focallen,
	         *ortho_scale;
} CameraWidgetGroup;


static bool WIDGETGROUP_lamp_poll(const bContext *C, wmManipulatorGroupType *UNUSED(wgrouptype))
{
	Object *ob = CTX_data_active_object(C);

	if (ob && ob->type == OB_LAMP) {
		Lamp *la = ob->data;
		return (la->type == LA_SPOT);
	}
	return false;
}

static void WIDGETGROUP_lamp_init(const bContext *UNUSED(C), wmManipulatorGroup *wgroup)
{
	const char *propname = "spot_size";

	const float color[4] = {0.5f, 0.5f, 1.0f, 1.0f};
	const float color_hi[4] = {0.8f, 0.8f, 0.45f, 1.0f};

	wmManipulatorWrapper *wwrapper = MEM_mallocN(sizeof(wmManipulatorWrapper), __func__);

	wwrapper->manipulator = MANIPULATOR_arrow_new(wgroup, propname, MANIPULATOR_ARROW_STYLE_INVERTED);
	wgroup->customdata = wwrapper;

	MANIPULATOR_arrow_set_range_fac(wwrapper->manipulator, 4.0f);
	WM_manipulator_set_colors(wwrapper->manipulator, color, color_hi);
}

static void WIDGETGROUP_lamp_refresh(const bContext *C, wmManipulatorGroup *wgroup)
{
	wmManipulatorWrapper *wwrapper = wgroup->customdata;
	Object *ob = CTX_data_active_object(C);
	Lamp *la = ob->data;
	float dir[3];

	negate_v3_v3(dir, ob->obmat[2]);

	MANIPULATOR_arrow_set_direction(wwrapper->manipulator, dir);
	WM_manipulator_set_origin(wwrapper->manipulator, ob->obmat[3]);

	/* need to set property here for undo. TODO would prefer to do this in _init */
	PointerRNA ptr;
	const char *propname = "spot_size";
	RNA_pointer_create(&la->id, &RNA_Lamp, la, &ptr);
	WM_manipulator_set_property(wwrapper->manipulator, ARROW_SLOT_OFFSET_WORLD_SPACE, &ptr, propname);
}

void VIEW3D_WGT_lamp(wmManipulatorGroupType *wgt)
{
	wgt->name = "Lamp Widgets";

	wgt->poll = WIDGETGROUP_lamp_poll;
	wgt->init = WIDGETGROUP_lamp_init;
	wgt->refresh = WIDGETGROUP_lamp_refresh;

	wgt->is_3d = true;
}

static bool WIDGETGROUP_camera_poll(const bContext *C, wmManipulatorGroupType *UNUSED(wgrouptype))
{
	Object *ob = CTX_data_active_object(C);

	return (ob && ob->type == OB_CAMERA);
}

static void cameragroup_property_setup(wmManipulator *widget, Object *ob, Camera *ca, const bool is_ortho)
{
	const float scale[3] = {1.0f / len_v3(ob->obmat[0]), 1.0f / len_v3(ob->obmat[1]), 1.0f / len_v3(ob->obmat[2])};
	const float scale_fac = ca->drawsize;
	const float drawsize = is_ortho ? (0.5f * ca->ortho_scale) :
	                                  (scale_fac / ((scale[0] + scale[1] + scale[2]) / 3.0f));
	const float half_sensor = 0.5f * ((ca->sensor_fit == CAMERA_SENSOR_FIT_VERT) ? ca->sensor_y : ca->sensor_x);
	const char *propname = is_ortho ? "ortho_scale" : "lens";

	PointerRNA cameraptr;
	float min, max, range;
	float step, precision;

	RNA_pointer_create(&ca->id, &RNA_Camera, ca, &cameraptr);

	/* get property range */
	PropertyRNA *prop = RNA_struct_find_property(&cameraptr, propname);
	RNA_property_float_ui_range(&cameraptr, prop, &min, &max, &step, &precision);
	range = max - min;

	MANIPULATOR_arrow_set_range_fac(widget, is_ortho ? (scale_fac * range) : (drawsize * range / half_sensor));
}

static void WIDGETGROUP_camera_init(const bContext *C, wmManipulatorGroup *wgroup)
{
	Object *ob = CTX_data_active_object(C);
	Camera *ca = ob->data;
	float dir[3];

	CameraWidgetGroup *camgroup = MEM_callocN(sizeof(CameraWidgetGroup), __func__);
	wgroup->customdata = camgroup;

	negate_v3_v3(dir, ob->obmat[2]);

	/* dof distance */
	{
		const float color[4] = {1.0f, 0.3f, 0.0f, 1.0f};
		const float color_hi[4] = {1.0f, 0.3f, 0.0f, 1.0f};

		camgroup->dop_dist = MANIPULATOR_arrow_new(wgroup, "dof_distance", MANIPULATOR_ARROW_STYLE_CROSS);
		WM_manipulator_set_flag(camgroup->dop_dist, WM_MANIPULATOR_DRAW_HOVER, true);
		WM_manipulator_set_flag(camgroup->dop_dist, WM_MANIPULATOR_SCALE_3D, false);
		WM_manipulator_set_colors(camgroup->dop_dist, color, color_hi);
	}

	/* focal length
	 * - logic/calculations are similar to BKE_camera_view_frame_ex, better keep in sync */
	{
		const float color[4] = {1.0f, 1.0, 0.27f, 0.5f};
		const float color_hi[4] = {1.0f, 1.0, 0.27f, 1.0f};

		camgroup->focallen = MANIPULATOR_arrow_new(
		                         wgroup, "focal_len",
		                         (MANIPULATOR_ARROW_STYLE_CONE | MANIPULATOR_ARROW_STYLE_CONSTRAINED));
		WM_manipulator_set_flag(camgroup->focallen, WM_MANIPULATOR_SCALE_3D, false);
		WM_manipulator_set_colors(camgroup->focallen, color, color_hi);
		cameragroup_property_setup(camgroup->focallen, ob, ca, false);

		camgroup->ortho_scale = MANIPULATOR_arrow_new(
		                            wgroup, "ortho_scale",
		                            (MANIPULATOR_ARROW_STYLE_CONE | MANIPULATOR_ARROW_STYLE_CONSTRAINED));
		WM_manipulator_set_flag(camgroup->ortho_scale, WM_MANIPULATOR_SCALE_3D, false);
		WM_manipulator_set_colors(camgroup->ortho_scale, color, color_hi);
		cameragroup_property_setup(camgroup->ortho_scale, ob, ca, true);
	}
}

static void WIDGETGROUP_camera_refresh(const bContext *C, wmManipulatorGroup *wgroup)
{
	if (!wgroup->customdata)
		return;

	CameraWidgetGroup *camgroup = wgroup->customdata;
	Object *ob = CTX_data_active_object(C);
	Camera *ca = ob->data;
	PointerRNA cameraptr;
	float dir[3];

	RNA_pointer_create(&ca->id, &RNA_Camera, ca, &cameraptr);

	negate_v3_v3(dir, ob->obmat[2]);

	if (ca->flag & CAM_SHOWLIMITS) {
		MANIPULATOR_arrow_set_direction(camgroup->dop_dist, dir);
		MANIPULATOR_arrow_set_up_vector(camgroup->dop_dist, ob->obmat[1]);
		WM_manipulator_set_origin(camgroup->dop_dist, ob->obmat[3]);
		WM_manipulator_set_scale(camgroup->dop_dist, ca->drawsize);
		WM_manipulator_set_flag(camgroup->dop_dist, WM_MANIPULATOR_HIDDEN, false);

		/* need to set property here for undo. TODO would prefer to do this in _init */
		WM_manipulator_set_property(camgroup->dop_dist, ARROW_SLOT_OFFSET_WORLD_SPACE, &cameraptr, "dof_distance");
	}
	else {
		WM_manipulator_set_flag(camgroup->dop_dist, WM_MANIPULATOR_HIDDEN, true);
	}

	/* TODO - make focal length/ortho scale widget optional */
	if (true) {
		const bool is_ortho = (ca->type == CAM_ORTHO);
		const float scale[3] = {1.0f / len_v3(ob->obmat[0]), 1.0f / len_v3(ob->obmat[1]), 1.0f / len_v3(ob->obmat[2])};
		const float scale_fac = ca->drawsize;
		const float drawsize = is_ortho ? (0.5f * ca->ortho_scale) :
		                                  (scale_fac / ((scale[0] + scale[1] + scale[2]) / 3.0f));
		float offset[3];
		float asp[2];

		wmManipulator *widget = is_ortho ? camgroup->ortho_scale : camgroup->focallen;
		WM_manipulator_set_flag(widget, WM_MANIPULATOR_HIDDEN, false);
		WM_manipulator_set_flag(is_ortho ? camgroup->focallen : camgroup->ortho_scale, WM_MANIPULATOR_HIDDEN, true);


		/* account for lens shifting */
		offset[0] = ((ob->size[0] > 0.0f) ? -2.0f : 2.0f) * ca->shiftx;
		offset[1] = 2.0f * ca->shifty;
		offset[2] = 0.0f;

		/* get aspect */
		const Scene *scene = CTX_data_scene(C);
		const float aspx = (float)scene->r.xsch * scene->r.xasp;
		const float aspy = (float)scene->r.ysch * scene->r.yasp;
		const int sensor_fit = BKE_camera_sensor_fit(ca->sensor_fit, aspx, aspy);
		asp[0] = (sensor_fit == CAMERA_SENSOR_FIT_HOR) ? 1.0 : aspx / aspy;
		asp[1] = (sensor_fit == CAMERA_SENSOR_FIT_HOR) ? aspy / aspx : 1.0f;

		MANIPULATOR_arrow_set_up_vector(widget, ob->obmat[1]);
		MANIPULATOR_arrow_set_direction(widget, dir);
		MANIPULATOR_arrow_cone_set_aspect(widget, asp);
		WM_manipulator_set_origin(widget, ob->obmat[3]);
		WM_manipulator_set_offset(widget, offset);
		WM_manipulator_set_scale(widget, drawsize);

		/* need to set property here for undo. TODO would prefer to do this in _init */
		WM_manipulator_set_property(camgroup->focallen, ARROW_SLOT_OFFSET_WORLD_SPACE, &cameraptr, "lens");
		WM_manipulator_set_property(camgroup->ortho_scale, ARROW_SLOT_OFFSET_WORLD_SPACE, &cameraptr, "ortho_scale");
	}
}

void VIEW3D_WGT_camera(wmManipulatorGroupType *wgt)
{
	wgt->name = "Camera Widgets";

	wgt->poll = WIDGETGROUP_camera_poll;
	wgt->init = WIDGETGROUP_camera_init;
	wgt->refresh = WIDGETGROUP_camera_refresh;

	wgt->is_3d = true;
}

static bool WIDGETGROUP_forcefield_poll(const bContext *C, wmManipulatorGroupType *UNUSED(wgrouptype))
{
	Object *ob = CTX_data_active_object(C);

	return (ob && ob->pd && ob->pd->forcefield);
}

static void WIDGETGROUP_forcefield_init(const bContext *UNUSED(C), wmManipulatorGroup *wgroup)
{
	const float col[4] = {0.8f, 0.8f, 0.45f, 0.5f};
	const float col_hi[4] = {0.8f, 0.8f, 0.45f, 1.0f};

	/* only wind effector for now */
	wmManipulatorWrapper *wwrapper = MEM_mallocN(sizeof(wmManipulatorWrapper), __func__);
	wgroup->customdata = wwrapper;

	wwrapper->manipulator = MANIPULATOR_arrow_new(wgroup, "field_strength", MANIPULATOR_ARROW_STYLE_CONSTRAINED);

	MANIPULATOR_arrow_set_ui_range(wwrapper->manipulator, -200.0f, 200.0f);
	MANIPULATOR_arrow_set_range_fac(wwrapper->manipulator, 6.0f);
	WM_manipulator_set_colors(wwrapper->manipulator, col, col_hi);
	WM_manipulator_set_flag(wwrapper->manipulator, WM_MANIPULATOR_SCALE_3D, false);
}

static void WIDGETGROUP_forcefield_refresh(const bContext *C, wmManipulatorGroup *wgroup)
{
	wmManipulatorWrapper *wwrapper = wgroup->customdata;
	Object *ob = CTX_data_active_object(C);
	PartDeflect *pd = ob->pd;

	if (pd->forcefield == PFIELD_WIND) {
		const float size = (ob->type == OB_EMPTY) ? ob->empty_drawsize : 1.0f;
		const float ofs[3] = {0.0f, -size, 0.0f};
		PointerRNA ptr;

		RNA_pointer_create(&ob->id, &RNA_FieldSettings, pd, &ptr);

		MANIPULATOR_arrow_set_direction(wwrapper->manipulator, ob->obmat[2]);
		WM_manipulator_set_origin(wwrapper->manipulator, ob->obmat[3]);
		WM_manipulator_set_offset(wwrapper->manipulator, ofs);
		WM_manipulator_set_flag(wwrapper->manipulator, WM_MANIPULATOR_HIDDEN, false);
		WM_manipulator_set_property(wwrapper->manipulator, ARROW_SLOT_OFFSET_WORLD_SPACE, &ptr, "strength");
	}
	else {
		WM_manipulator_set_flag(wwrapper->manipulator, WM_MANIPULATOR_HIDDEN, true);
	}
}

void VIEW3D_WGT_force_field(wmManipulatorGroupType *wgt)
{
	wgt->name = "Force Field Widgets";

	wgt->poll = WIDGETGROUP_forcefield_poll;
	wgt->init = WIDGETGROUP_forcefield_init;
	wgt->refresh = WIDGETGROUP_forcefield_refresh;

	wgt->is_3d = true;
}

/* draw facemaps depending on the selected bone in pose mode */
#define USE_FACEMAP_FROM_BONE

#define MAX_ARMATURE_FACEMAP_NAME (2 * MAX_NAME + 1) /* "OBJECTNAME_FACEMAPNAME" */


static bool WIDGETGROUP_armature_facemaps_poll(const bContext *C, wmManipulatorGroupType *UNUSED(wgrouptype))
{
	Object *ob = CTX_data_active_object(C);

#ifdef USE_FACEMAP_FROM_BONE
	if (ob && BKE_object_pose_context_check(ob)) {
		for (bPoseChannel *pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
			if (pchan->fmap_object && pchan->fmap) {
				return true;
			}
		}
	}
#else
	if (ob && ob->type == OB_MESH && ob->fmaps.first) {
		ModifierData *md;
		VirtualModifierData virtualModifierData;

		md = modifiers_getVirtualModifierList(ob, &virtualModifierData);

		/* exception for shape keys because we can edit those */
		for (; md; md = md->next) {
			if (modifier_isEnabled(CTX_data_scene(C), md, eModifierMode_Realtime) &&
			    md->type == eModifierType_Armature)
			{
				ArmatureModifierData *amd = (ArmatureModifierData *) md;
				if (amd->object && (amd->deformflag & ARM_DEF_FACEMAPS))
					return true;
			}
		}
	}
#endif

	return false;
}

static void WIDGET_armature_facemaps_select(bContext *C, wmManipulator *widget, const int action)
{
	Object *ob = CTX_data_active_object(C);

	switch (action) {
		case SEL_SELECT:
			for (bPoseChannel *pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
				if (pchan->fmap == MANIPULATOR_facemap_get_fmap(widget)) {
					/* deselect all first */
					ED_pose_de_selectall(ob, SEL_DESELECT, false);
					ED_pose_bone_select(ob, pchan, true);
				}
			}
			break;
		default:
			BLI_assert(0);
	}
}

/**
 * Get a string that equals a string generated using #armature_facemap_hashname_create,
 * but without allocating it. Only use for comparing with string stored as hash key.
 */
BLI_INLINE void armature_facemap_hashkey_get(
        Object *fmap_ob, bFaceMap *fmap, size_t maxname,
        char *r_hashkey)
{
	BLI_snprintf_rlen(r_hashkey, maxname, "%s_%s", fmap_ob->id.name + 2, fmap->name);
}

/**
 * Same as #armature_facemap_hashname_get but allocates a new string. Use for storing string as hash key.
 * \return A string using "OBJECTNAME_FACEMAPNAME" format.
 */
BLI_INLINE char *armature_facemap_hashkey_create(Object *fmap_ob, bFaceMap *fmap)
{
	return BLI_sprintfN("%s_%s", fmap_ob->id.name + 2, fmap->name);
}

BLI_INLINE void armature_facemap_ghash_insert(GHash *hash, wmManipulator *widget, Object *fmap_ob, bFaceMap *fmap)
{
	BLI_ghash_insert(hash, armature_facemap_hashkey_create(fmap_ob, fmap), widget);
}

/**
 * Free armature facemap ghash, used as freeing callback for wmManipulatorGroup.customdata.
 */
BLI_INLINE void armature_facemap_ghash_free(void *customdata)
{
	BLI_ghash_free(customdata, MEM_freeN, NULL);
}

static wmManipulator *armature_facemap_widget_create(wmManipulatorGroup *wgroup, Object *fmap_ob, bFaceMap *fmap)
{
	wmManipulator *widget = MANIPULATOR_facemap_new(wgroup, fmap->name, 0, fmap_ob, BLI_findindex(&fmap_ob->fmaps, fmap));

	WM_manipulator_set_operator(widget, "TRANSFORM_OT_translate");
	WM_manipulator_set_flag(widget, WM_MANIPULATOR_DRAW_HOVER, true);
	WM_manipulator_set_func_select(widget, WIDGET_armature_facemaps_select);
	PointerRNA *opptr = WM_manipulator_set_operator(widget, "TRANSFORM_OT_translate");
	RNA_boolean_set(opptr, "release_confirm", true);

	return widget;
}

static void WIDGETGROUP_armature_facemaps_init(const bContext *C, wmManipulatorGroup *wgroup)
{
	Object *ob = CTX_data_active_object(C);
	bArmature *arm = (bArmature *)ob->data;

#ifdef USE_FACEMAP_FROM_BONE
	bPoseChannel *pchan;
	GHash *hash = BLI_ghash_str_new(__func__);

	for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
		if (pchan->fmap && (pchan->bone->layer & arm->layer)) {
			wmManipulator *widget = armature_facemap_widget_create(wgroup, pchan->fmap_object, pchan->fmap);
			armature_facemap_ghash_insert(hash, widget, pchan->fmap_object, pchan->fmap);
		}
	}
	wgroup->customdata = hash;
	wgroup->customdata_free = armature_facemap_ghash_free;
#else
	Object *armature;
	ModifierData *md;
	VirtualModifierData virtualModifierData;
	int index = 0;
	bFaceMap *fmap = ob->fmaps.first;


	md = modifiers_getVirtualModifierList(ob, &virtualModifierData);

	/* exception for shape keys because we can edit those */
	for (; md; md = md->next) {
		if (modifier_isEnabled(CTX_data_scene(C), md, eModifierMode_Realtime) && md->type == eModifierType_Armature) {
			ArmatureModifierData *amd = (ArmatureModifierData *) md;
			if (amd->object && (amd->deformflag & ARM_DEF_FACEMAPS)) {
				armature = amd->object;
				break;
			}
		}
	}


	for (; fmap; fmap = fmap->next, index++) {
		if (BKE_pose_channel_find_name(armature->pose, fmap->name)) {
			PointerRNA *opptr;

			widget = MANIPULATOR_facemap_new(wgroup, fmap->name, 0, ob, index);

			RNA_pointer_create(&ob->id, &RNA_FaceMap, fmap, &famapptr);
			WM_manipulator_set_colors(widget, color_shape, color_shape);
			WM_manipulator_set_flag(widget, WM_MANIPULATOR_DRAW_HOVER, true);
			opptr = WM_manipulator_set_operator(widget, "TRANSFORM_OT_translate");
			if ((prop = RNA_struct_find_property(opptr, "release_confirm"))) {
				RNA_property_boolean_set(opptr, prop, true);
			}
		}
	}
#endif
}

/**
 * We do some special stuff for refreshing facemap widgets nicely:
 * * On widget group init, needed widgets are created and stored in a hash table (wmManipulatorGroup.customdata).
 * * On widget group refresh, a new hash table is created and compared to the old one. For each widget needed we
 *   check if it's already existing in the old hash table, if so it's moved to the new one, if not it gets created.
 * * The remaining widgets in the old hash table get completely deleted, the old hash table gets deleted, the new
 *   one is stored (wmManipulatorGroup.customdata) and becomes the old one on next refresh.
 */
static void WIDGETGROUP_armature_facemaps_refresh(const bContext *C, wmManipulatorGroup *wgroup)
{
	if (!wgroup->customdata)
		return;

	Object *ob = CTX_data_active_object(C);
	bArmature *arm = (bArmature *)ob->data;
	ARegion *ar = CTX_wm_region(C);

#ifdef USE_FACEMAP_FROM_BONE
	/* we create a new hash from the visible members of the old hash */
	GHash *oldhash = wgroup->customdata;
	GHash *newhash = BLI_ghash_str_new(__func__);
	wmManipulator *widget;

	for (bPoseChannel *pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
		if (!pchan->fmap)
			continue;

		char widgetkey[MAX_ARMATURE_FACEMAP_NAME];
		armature_facemap_hashkey_get(pchan->fmap_object, pchan->fmap, sizeof(widgetkey), widgetkey);

		/* create new widget for newly assigned facemap, add it to new hash */
		if (!(widget = BLI_ghash_lookup(oldhash, widgetkey))) {
			widget = armature_facemap_widget_create(wgroup, pchan->fmap_object, pchan->fmap);
			BLI_assert(widget);
		}
		armature_facemap_ghash_insert(newhash, widget, pchan->fmap_object, pchan->fmap);

		if ((pchan->bone->layer & arm->layer)) {
			const ThemeWireColor *bcol = ED_pchan_get_colorset(arm, ob->pose, pchan);
			float col[4] = {0.8f, 0.8f, 0.45f, 0.2f};
			float col_hi[4] = {0.8f, 0.8f, 0.45f, 0.4f};
			/* get custom bone group color */
			if (bcol) {
				rgb_uchar_to_float(col, (unsigned char *)bcol->solid);
				rgb_uchar_to_float(col_hi, (unsigned char *)bcol->active);
			}
			WM_manipulator_set_colors(widget, col, col_hi);
			WM_manipulator_set_flag(widget, WM_MANIPULATOR_HIDDEN, false);
		}
		else {
			WM_manipulator_set_flag(widget, WM_MANIPULATOR_HIDDEN, true);
		}

		/* remove from old hash */
		BLI_ghash_remove(oldhash, widgetkey, MEM_freeN, NULL);
	}

	/* remove remaining widgets from old hash */
	GHashIterator ghi;
	GHASH_ITER(ghi, oldhash) {
		wmManipulator *found = BLI_ghashIterator_getValue(&ghi);
		WM_manipulator_delete(&wgroup->manipulators, ar->manipulator_map, found, (bContext *)C);
	}
	armature_facemap_ghash_free(oldhash);

	wgroup->customdata = newhash;
#endif
}

void VIEW3D_WGT_armature_facemaps(wmManipulatorGroupType *wgt)
{
	wgt->name = "Face Map Widgets";

	wgt->poll = WIDGETGROUP_armature_facemaps_poll;
	wgt->init = WIDGETGROUP_armature_facemaps_init;
	wgt->refresh = WIDGETGROUP_armature_facemaps_refresh;

	wgt->keymap_init = WM_manipulatorgroup_keymap_common_sel;

	wgt->is_3d = true;
}
