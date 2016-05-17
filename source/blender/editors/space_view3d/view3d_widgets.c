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
#include "DNA_object_types.h"
#include "DNA_object_force.h"
#include "DNA_lamp_types.h"

#include "ED_armature.h"
#include "ED_screen.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

#include "view3d_intern.h"  /* own include */

typedef struct CameraWidgetGroup {
	wmWidget *dop_dist,
	         *focallen,
	         *ortho_scale;
} CameraWidgetGroup;


int WIDGETGROUP_lamp_poll(const bContext *C, wmWidgetGroupType *UNUSED(wgrouptype))
{
	Object *ob = CTX_data_active_object(C);

	if (ob && ob->type == OB_LAMP) {
		Lamp *la = ob->data;
		return (la->type == LA_SPOT);
	}
	return false;
}

void WIDGETGROUP_lamp_init(const bContext *C, wmWidgetGroup *wgroup)
{
	Object *ob = CTX_data_active_object(C);
	Lamp *la = ob->data;
	PointerRNA ptr;
	const char *propname = "spot_size";

	const float color[4] = {0.5f, 0.5f, 1.0f, 1.0f};
	const float color_hi[4] = {0.8f, 0.8f, 0.45f, 1.0f};

	wmWidgetWrapper *wwrapper = MEM_mallocN(sizeof(wmWidgetWrapper), __func__);

	wwrapper->widget = WIDGET_arrow_new(wgroup, propname, WIDGET_ARROW_STYLE_INVERTED);
	wgroup->customdata = wwrapper;

	RNA_pointer_create(&la->id, &RNA_Lamp, la, &ptr);
	WIDGET_arrow_set_range_fac(wwrapper->widget, 4.0f);
	WM_widget_set_colors(wwrapper->widget, color, color_hi);
	WM_widget_set_property(wwrapper->widget, ARROW_SLOT_OFFSET_WORLD_SPACE, &ptr, propname);
}

void WIDGETGROUP_lamp_refresh(const bContext *C, wmWidgetGroup *wgroup)
{
	wmWidgetWrapper *wwrapper = wgroup->customdata;
	Object *ob = CTX_data_active_object(C);
	float dir[3];

	negate_v3_v3(dir, ob->obmat[2]);

	WIDGET_arrow_set_direction(wwrapper->widget, dir);
	WM_widget_set_origin(wwrapper->widget, ob->obmat[3]);
}

int WIDGETGROUP_camera_poll(const bContext *C, wmWidgetGroupType *UNUSED(wgrouptype))
{
	Object *ob = CTX_data_active_object(C);

	return (ob && ob->type == OB_CAMERA);
}

static void cameragroup_property_setup(wmWidget *widget, Object *ob, Camera *ca, const bool is_ortho)
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

	WIDGET_arrow_set_range_fac(widget, is_ortho ? (scale_fac * range) : (drawsize * range / half_sensor));
}

void WIDGETGROUP_camera_init(const bContext *C, wmWidgetGroup *wgroup)
{
	Object *ob = CTX_data_active_object(C);
	Camera *ca = ob->data;
	PointerRNA cameraptr;
	float dir[3];

	CameraWidgetGroup *camgroup = MEM_callocN(sizeof(CameraWidgetGroup), __func__);
	wgroup->customdata = camgroup;

	RNA_pointer_create(&ca->id, &RNA_Camera, ca, &cameraptr);
	negate_v3_v3(dir, ob->obmat[2]);

	/* dof distance */
	{
		const float color[4] = {1.0f, 0.3f, 0.0f, 1.0f};
		const float color_hi[4] = {1.0f, 0.3f, 0.0f, 1.0f};
		const char *propname = "dof_distance";

		camgroup->dop_dist = WIDGET_arrow_new(wgroup, propname, WIDGET_ARROW_STYLE_CROSS);
		WM_widget_set_flag(camgroup->dop_dist, WM_WIDGET_DRAW_HOVER, true);
		WM_widget_set_flag(camgroup->dop_dist, WM_WIDGET_SCALE_3D, false);
		WM_widget_set_colors(camgroup->dop_dist, color, color_hi);
		WM_widget_set_property(camgroup->dop_dist, ARROW_SLOT_OFFSET_WORLD_SPACE, &cameraptr, propname);
	}

	/* focal length
	 * - logic/calculations are similar to BKE_camera_view_frame_ex, better keep in sync */
	{
		const float color[4] = {1.0f, 1.0, 0.27f, 0.5f};
		const float color_hi[4] = {1.0f, 1.0, 0.27f, 1.0f};

		camgroup->focallen = WIDGET_arrow_new(
		                         wgroup, "focal_len",
		                         (WIDGET_ARROW_STYLE_CONE | WIDGET_ARROW_STYLE_CONSTRAINED));
		WM_widget_set_flag(camgroup->focallen, WM_WIDGET_SCALE_3D, false);
		WM_widget_set_colors(camgroup->focallen, color, color_hi);
		cameragroup_property_setup(camgroup->focallen, ob, ca, false);
		WM_widget_set_property(camgroup->focallen, ARROW_SLOT_OFFSET_WORLD_SPACE, &cameraptr, "lens");

		camgroup->ortho_scale = WIDGET_arrow_new(
		                            wgroup, "ortho_scale",
		                            (WIDGET_ARROW_STYLE_CONE | WIDGET_ARROW_STYLE_CONSTRAINED));
		WM_widget_set_flag(camgroup->ortho_scale, WM_WIDGET_SCALE_3D, false);
		WM_widget_set_colors(camgroup->ortho_scale, color, color_hi);
		cameragroup_property_setup(camgroup->ortho_scale, ob, ca, true);
		WM_widget_set_property(camgroup->ortho_scale, ARROW_SLOT_OFFSET_WORLD_SPACE, &cameraptr, "ortho_scale");
	}
}

void WIDGETGROUP_camera_refresh(const bContext *C, wmWidgetGroup *wgroup)
{
	if (!wgroup->customdata)
		return;

	CameraWidgetGroup *camgroup = wgroup->customdata;
	Object *ob = CTX_data_active_object(C);
	Camera *ca = ob->data;
	float dir[3];

	negate_v3_v3(dir, ob->obmat[2]);

	if (ca->flag & CAM_SHOWLIMITS) {
		WIDGET_arrow_set_direction(camgroup->dop_dist, dir);
		WIDGET_arrow_set_up_vector(camgroup->dop_dist, ob->obmat[1]);
		WM_widget_set_origin(camgroup->dop_dist, ob->obmat[3]);
		WM_widget_set_scale(camgroup->dop_dist, ca->drawsize);
		WM_widget_set_flag(camgroup->dop_dist, WM_WIDGET_HIDDEN, false);
	}
	else {
		WM_widget_set_flag(camgroup->dop_dist, WM_WIDGET_HIDDEN, true);
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

		wmWidget *widget = is_ortho ? camgroup->ortho_scale : camgroup->focallen;
		WM_widget_set_flag(widget, WM_WIDGET_HIDDEN, false);
		WM_widget_set_flag(is_ortho ? camgroup->focallen : camgroup->ortho_scale, WM_WIDGET_HIDDEN, true);


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

		WIDGET_arrow_set_up_vector(widget, ob->obmat[1]);
		WIDGET_arrow_set_direction(widget, dir);
		WIDGET_arrow_cone_set_aspect(widget, asp);
		WM_widget_set_origin(widget, ob->obmat[3]);
		WM_widget_set_offset(widget, offset);
		WM_widget_set_scale(widget, drawsize);
	}
}

int WIDGETGROUP_forcefield_poll(const bContext *C, wmWidgetGroupType *UNUSED(wgrouptype))
{
	Object *ob = CTX_data_active_object(C);

	return (ob && ob->pd && ob->pd->forcefield);
}

void WIDGETGROUP_forcefield_init(const bContext *UNUSED(C), wmWidgetGroup *wgroup)
{
	const float col[4] = {0.8f, 0.8f, 0.45f, 0.5f};
	const float col_hi[4] = {0.8f, 0.8f, 0.45f, 1.0f};

	/* only wind effector for now */
	wmWidgetWrapper *wwrapper = MEM_mallocN(sizeof(wmWidgetWrapper), __func__);
	wgroup->customdata = wwrapper;

	wwrapper->widget = WIDGET_arrow_new(wgroup, "field_strength", WIDGET_ARROW_STYLE_CONSTRAINED);

	WIDGET_arrow_set_ui_range(wwrapper->widget, -200.0f, 200.0f);
	WIDGET_arrow_set_range_fac(wwrapper->widget, 6.0f);
	WM_widget_set_colors(wwrapper->widget, col, col_hi);
	WM_widget_set_flag(wwrapper->widget, WM_WIDGET_SCALE_3D, false);
}

void WIDGETGROUP_forcefield_refresh(const bContext *C, wmWidgetGroup *wgroup)
{
	wmWidgetWrapper *wwrapper = wgroup->customdata;
	Object *ob = CTX_data_active_object(C);
	PartDeflect *pd = ob->pd;

	if (pd->forcefield == PFIELD_WIND) {
		const float size = (ob->type == OB_EMPTY) ? ob->empty_drawsize : 1.0f;
		const float ofs[3] = {0.0f, -size, 0.0f};
		PointerRNA ptr;

		RNA_pointer_create(&ob->id, &RNA_FieldSettings, pd, &ptr);

		WIDGET_arrow_set_direction(wwrapper->widget, ob->obmat[2]);
		WM_widget_set_origin(wwrapper->widget, ob->obmat[3]);
		WM_widget_set_offset(wwrapper->widget, ofs);
		WM_widget_set_flag(wwrapper->widget, WM_WIDGET_HIDDEN, false);
		WM_widget_set_property(wwrapper->widget, ARROW_SLOT_OFFSET_WORLD_SPACE, &ptr, "strength");
	}
	else {
		WM_widget_set_flag(wwrapper->widget, WM_WIDGET_HIDDEN, true);
	}
}

/* draw facemaps depending on the selected bone in pose mode */
#define USE_FACEMAP_FROM_BONE

#define MAX_ARMATURE_FACEMAP_NAME (2 * MAX_NAME + 1) /* "OBJECTNAME_FACEMAPNAME" */


int WIDGETGROUP_armature_facemaps_poll(const bContext *C, wmWidgetGroupType *UNUSED(wgrouptype))
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

static void WIDGET_armature_facemaps_select(bContext *C, wmWidget *widget, const int action)
{
	Object *ob = CTX_data_active_object(C);

	switch (action) {
		case SEL_SELECT:
			for (bPoseChannel *pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
				if (pchan->fmap == WIDGET_facemap_get_fmap(widget)) {
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

BLI_INLINE void armature_facemap_ghash_insert(GHash *hash, wmWidget *widget, Object *fmap_ob, bFaceMap *fmap)
{
	BLI_ghash_insert(hash, armature_facemap_hashkey_create(fmap_ob, fmap), widget);
}

/**
 * Free armature facemap ghash, used as freeing callback for wmWidgetGroup.customdata.
 */
BLI_INLINE void armature_facemap_ghash_free(void *customdata)
{
	BLI_ghash_free(customdata, MEM_freeN, NULL);
}

static wmWidget *armature_facemap_widget_create(wmWidgetGroup *wgroup, Object *fmap_ob, bFaceMap *fmap)
{
	wmWidget *widget = WIDGET_facemap_new(wgroup, fmap->name, 0, fmap_ob, BLI_findindex(&fmap_ob->fmaps, fmap));

	WM_widget_set_operator(widget, "TRANSFORM_OT_translate");
	WM_widget_set_flag(widget, WM_WIDGET_DRAW_HOVER, true);
	WM_widget_set_func_select(widget, WIDGET_armature_facemaps_select);
	PointerRNA *opptr = WM_widget_set_operator(widget, "TRANSFORM_OT_translate");
	RNA_boolean_set(opptr, "release_confirm", true);

	return widget;
}

void WIDGETGROUP_armature_facemaps_init(const bContext *C, wmWidgetGroup *wgroup)
{
	Object *ob = CTX_data_active_object(C);
	bArmature *arm = (bArmature *)ob->data;

#ifdef USE_FACEMAP_FROM_BONE
	bPoseChannel *pchan;
	GHash *hash = BLI_ghash_str_new(__func__);

	for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
		if (pchan->fmap && (pchan->bone->layer & arm->layer)) {
			wmWidget *widget = armature_facemap_widget_create(wgroup, pchan->fmap_object, pchan->fmap);
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

			widget = WIDGET_facemap_new(wgroup, fmap->name, 0, ob, index);

			RNA_pointer_create(&ob->id, &RNA_FaceMap, fmap, &famapptr);
			WM_widget_set_colors(widget, color_shape, color_shape);
			WM_widget_set_flag(widget, WM_WIDGET_DRAW_HOVER, true);
			opptr = WM_widget_set_operator(widget, "TRANSFORM_OT_translate");
			if ((prop = RNA_struct_find_property(opptr, "release_confirm"))) {
				RNA_property_boolean_set(opptr, prop, true);
			}
		}
	}
#endif
}

/**
 * We do some special stuff for refreshing facemap widgets nicely:
 * * On widget group init, needed widgets are created and stored in a hash table (wmWidgetGroup.customdata).
 * * On widget group refresh, a new hash table is created and compared to the old one. For each widget needed we
 *   check if it's already existing in the old hash table, if so it's moved to the new one, if not it gets created.
 * * The remaining widgets in the old hash table get completely deleted, the old hash table gets deleted, the new
 *   one is stored (wmWidgetGroup.customdata) and becomes the old one on next refresh.
 */
void WIDGETGROUP_armature_facemaps_refresh(const bContext *C, wmWidgetGroup *wgroup)
{
	if (!wgroup->customdata)
		return;

	Object *ob = CTX_data_active_object(C);
	bArmature *arm = (bArmature *)ob->data;

#ifdef USE_FACEMAP_FROM_BONE
	/* we create a new hash from the visible members of the old hash */
	GHash *oldhash = wgroup->customdata;
	GHash *newhash = BLI_ghash_str_new(__func__);
	wmWidget *widget;

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
			WM_widget_set_colors(widget, col, col_hi);
			WM_widget_set_flag(widget, WM_WIDGET_HIDDEN, false);
		}
		else {
			WM_widget_set_flag(widget, WM_WIDGET_HIDDEN, true);
		}

		/* remove from old hash */
		BLI_ghash_remove(oldhash, widgetkey, MEM_freeN, NULL);
	}

	/* remove remaining widgets from old hash */
	GHashIterator ghi;
	wmWidgetMap *wmap = WM_widgetmap_find(CTX_wm_region(C), &(const struct wmWidgetMapType_Params) {
	        "View3D", SPACE_VIEW3D, RGN_TYPE_WINDOW, WM_WIDGETMAPTYPE_3D});
	GHASH_ITER(ghi, oldhash) {
		wmWidget *found = BLI_ghashIterator_getValue(&ghi);
		WM_widget_delete(&wgroup->widgets, wmap, found, (bContext *)C);
	}
	armature_facemap_ghash_free(oldhash);

	wgroup->customdata = newhash;
#endif
}
