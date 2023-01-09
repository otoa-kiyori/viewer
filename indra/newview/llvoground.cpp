/** 
 * @file llvoground.cpp
 * @brief LLVOGround class implementation
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llvoground.h"
#include "lldrawpoolground.h"

#include "llviewercontrol.h"

#include "lldrawable.h"
#include "llface.h"
#include "llsky.h"
#include "llviewercamera.h"
#include "llviewerregion.h"
#include "pipeline.h"

LLVOGround::LLVOGround(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp)
:	LLStaticViewerObject(id, pcode, regionp, TRUE)
{
	mbCanSelect = FALSE;
}


LLVOGround::~LLVOGround()
{
}

void LLVOGround::idleUpdate(LLAgent &agent, const F64 &time)
{
}


void LLVOGround::updateTextures()
{
}


LLDrawable *LLVOGround::createDrawable(LLPipeline *pipeline)
{
	pipeline->allocDrawable(this);
	mDrawable->setLit(FALSE);

	mDrawable->setRenderType(LLPipeline::RENDER_TYPE_GROUND);
	LLDrawPoolGround *poolp = (LLDrawPoolGround*) gPipeline.getPool(LLDrawPool::POOL_GROUND);

	mDrawable->addFace(poolp, NULL);

	return mDrawable;
}

// TO DO - this always returns TRUE, 
BOOL LLVOGround::updateGeometry(LLDrawable *drawable)
{
	LLFace *face;	

	LLDrawPoolGround *poolp = (LLDrawPoolGround*) gPipeline.getPool(LLDrawPool::POOL_GROUND);

	if (drawable->getNumFaces() < 1)
		drawable->addFace(poolp, NULL);
	face = drawable->getFace(0); 
	if (!face)
		return TRUE;
		
	face->setSize(5, 12);
	LLVertexBuffer* buff = new LLVertexBuffer(LLDrawPoolGround::VERTEX_DATA_MASK, GL_STREAM_DRAW);
    buff->allocateBuffer(face->getGeomCount(), face->getIndicesCount(), TRUE);
	
    LLMappedVertexData md = buff->mapVertexBuffer();
    U16* indicesp = buff->mapIndexBuffer();

    LLVector4a* verticesp = md.mPosition;
    LLVector2* texCoordsp = md.mTexCoord0;

	///////////////////////////////////////
	//
	//
	//
	LLVector3 at_dir = LLViewerCamera::getInstance()->getAtAxis();
	at_dir.mV[VZ] = 0.f;
	if (at_dir.normVec() < 0.01)
	{
		// We really don't care, as we're not looking anywhere near the horizon.
	}
	LLVector3 left_dir = LLViewerCamera::getInstance()->getLeftAxis();
	left_dir.mV[VZ] = 0.f;
	left_dir.normVec();

	// Our center top point
	LLColor4 ground_color = gSky.getSkyFogColor();
	ground_color.mV[3] = 1.f;
	face->setFaceColor(ground_color);
	
	*(verticesp++)  = LLVector4a(64, 64, 0);
	*(verticesp++)  = LLVector4a(-64, 64, 0);
	*(verticesp++)  = LLVector4a(-64, -64, 0);
	*(verticesp++)  = LLVector4a(64, -64, 0);
	*(verticesp++)  = LLVector4a(0, 0, -1024);
	
	
	// Triangles for each side
	*indicesp++ = 0;
	*indicesp++ = 1;
	*indicesp++ = 4;

	*indicesp++ = 1;
	*indicesp++ = 2;
	*indicesp++ = 4;

	*indicesp++ = 2;
	*indicesp++ = 3;
	*indicesp++ = 4;

	*indicesp++ = 3;
	*indicesp++ = 0;
	*indicesp++ = 4;

	*(texCoordsp++) = LLVector2(0.f, 0.f);
	*(texCoordsp++) = LLVector2(1.f, 0.f);
	*(texCoordsp++) = LLVector2(1.f, 1.f);
	*(texCoordsp++) = LLVector2(0.f, 1.f);
	*(texCoordsp++) = LLVector2(0.5f, 0.5f);
	
    buff->unmapIndexBuffer();
    buff->unmapVertexBuffer();

	LLPipeline::sCompiles++;
	return TRUE;
}
