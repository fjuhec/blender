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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2016 Kévin Dietrich.
 * All rights reserved.
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

#pragma once

#include <Alembic/Abc/All.h>

struct CustomData;
struct MPoly;

using Alembic::Abc::OCompoundProperty;

struct UVSample {
	std::vector<Imath::V2f> uvs;
	std::vector<uint32_t> indices;
};

struct CDWriterConfig {
	MPoly *mpoly;
	int totpoly;

	bool pack_uvs;
};

void get_uv_sample(UVSample &sample, const CDWriterConfig &config, CustomData *data);

void write_custom_data(const OCompoundProperty &prop,
                       const CDWriterConfig &config,
                       CustomData *data,
                       int data_type);
