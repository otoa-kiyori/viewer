/** 
 * @file llvertexbuffer.h
 * @brief LLVertexBuffer wrapper for OpengGL vertex buffer objects
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#ifndef LL_LLVERTEXBUFFER_H
#define LL_LLVERTEXBUFFER_H

#include "llgl.h"
#include "v2math.h"
#include "v3math.h"
#include "v4math.h"
#include "v4coloru.h"
#include "llstrider.h"
#include "llrender.h"
#include "lltrace.h"
#include <set>
#include <vector>
#include <list>

#define LL_MAX_VERTEX_ATTRIB_LOCATION 64

//============================================================================
// NOTES
// Threading:
//  All constructors should take an 'create' paramater which should only be
//  'true' when called from the main thread. Otherwise createGLBuffer() will
//  be called as soon as getVertexPointer(), etc is called (which MUST ONLY be
//  called from the main (i.e OpenGL) thread)

//============================================================================
// base class 
class LLPrivateMemoryPool;

// struct used for writing to and reading from vertex buffers with minimal calls to mapbuffer
// see getMappedVertexData
struct LLMappedVertexData
{
    LLVector4a* volatile mPosition = nullptr;
    LLVector2* volatile mTexCoord0 = nullptr;
    LLVector2* volatile mTexCoord1 = nullptr;
    LLVector2* volatile mTexCoord2 = nullptr;
    LLVector2* volatile mTexCoord3 = nullptr;
    LLVector4a* volatile mNormal = nullptr;
    LLVector4a* volatile mTangent = nullptr;
    LLColor4U* volatile mColor = nullptr;
    LLColor4U* volatile mEmissive = nullptr;
    F32* volatile mWeight = nullptr;
    LLVector4a* volatile mWeight4 = nullptr;
};

class LLVertexBuffer : public LLRefCount
{
public:
	class MappedRegion
	{
	public:
		S32 mType;
		S32 mIndex;
		S32 mCount;
		S32 mEnd;
		
		MappedRegion(S32 type, S32 index, S32 count);
	};

	LLVertexBuffer(const LLVertexBuffer& rhs)
	:	mUsage(rhs.mUsage)
	{
		*this = rhs;
	}

	const LLVertexBuffer& operator=(const LLVertexBuffer& rhs)
	{
		LL_ERRS() << "Illegal operation!" << LL_ENDL;
		return *this;
	}

	static void initClass(LLWindow* window);
	static void cleanupClass();
	static void setupClientArrays(U32 data_mask);
	static void drawArrays(U32 mode, const std::vector<LLVector3>& pos);
	static void drawElements(U32 mode, const LLVector4a* pos, const LLVector2* tc, S32 num_indices, const U16* indicesp);

 	static void unbind(); //unbind any bound vertex buffer

	//get the size of a vertex with the given typemask
	static S32 calcVertexSize(const U32& typemask);

	//get the size of a buffer with the given typemask and vertex count
	//fill offsets with the offset of each vertex component array into the buffer
	// indexed by the following enum
	static S32 calcOffsets(const U32& typemask, S32* offsets, S32 num_vertices);

	//WARNING -- when updating these enums you MUST 
	// 1 - update LLVertexBuffer::sTypeSize
    // 2 - update LLVertexBuffer::vb_type_name
	// 3 - add a strider accessor
	// 4 - modify LLVertexBuffer::setupVertexBuffer
    // 5 - modify LLVertexBuffer::setupVertexBufferFast
	// 6 - modify LLViewerShaderMgr::mReservedAttribs

    // clang-format off
    enum {                      // Shader attribute name, set in LLShaderMgr::initAttribsAndUniforms()
        TYPE_VERTEX = 0,        //  "position"
        TYPE_NORMAL,            //  "normal"
        TYPE_TEXCOORD0,         //  "texcoord0"
        TYPE_TEXCOORD1,         //  "texcoord1"
        TYPE_TEXCOORD2,         //  "texcoord2"
        TYPE_TEXCOORD3,         //  "texcoord3"
        TYPE_COLOR,             //  "diffuse_color"
        TYPE_EMISSIVE,          //  "emissive"
        TYPE_TANGENT,           //  "tangent"
        TYPE_WEIGHT,            //  "weight"
        TYPE_WEIGHT4,           //  "weight4"
        TYPE_TEXTURE_INDEX,     //  "texture_index"
        TYPE_MAX,   // TYPE_MAX is the size/boundary marker for attributes that go in the vertex buffer
        TYPE_INDEX,	// TYPE_INDEX is beyond _MAX because it lives in a separate (index) buffer	
    };
    // clang-format on

    enum {
		MAP_VERTEX = (1<<TYPE_VERTEX),
		MAP_NORMAL = (1<<TYPE_NORMAL),
		MAP_TEXCOORD0 = (1<<TYPE_TEXCOORD0),
		MAP_TEXCOORD1 = (1<<TYPE_TEXCOORD1),
		MAP_TEXCOORD2 = (1<<TYPE_TEXCOORD2),
		MAP_TEXCOORD3 = (1<<TYPE_TEXCOORD3),
		MAP_COLOR = (1<<TYPE_COLOR),
		MAP_EMISSIVE = (1<<TYPE_EMISSIVE),
		MAP_TANGENT = (1<<TYPE_TANGENT),
		MAP_WEIGHT = (1<<TYPE_WEIGHT),
		MAP_WEIGHT4 = (1<<TYPE_WEIGHT4),
		MAP_TEXTURE_INDEX = (1<<TYPE_TEXTURE_INDEX),
	};
	
protected:
	friend class LLRender;

	~LLVertexBuffer(); // use unref()

	void setupVertexBuffer(U32 data_mask);

	void	genBuffer(U32 size);
	void	genIndices(U32 size);
	void	bindGLBuffer();
	void	bindGLIndices();
	void	releaseBuffer();
	void	releaseIndices();
	bool	createGLBuffer(U32 size);
	bool	createGLIndices(U32 size);
	void 	destroyGLBuffer();
	void 	destroyGLIndices();
	bool	updateNumVerts(S32 nverts);
	bool	updateNumIndices(S32 nindices); 
		
public:
	LLVertexBuffer(U32 typemask, S32 usage);
	
	// map for data access
    // return an LLMappedVertexData struct for this buffer
    // any data not present will have its pointers set to null
    LLMappedVertexData mapVertexBuffer();
    void unmapVertexBuffer();

    bool isMappable() const;

    U16* mapIndexBuffer();
    void unmapIndexBuffer();

    // set position data via glBufferSubData
    // data must point to an array of LLVector4a of at least getNumVerts length
    void setPositionData(const LLVector4a* data);

    // set texcoord0 data via glBufferSubData
    // data must point to an array of LLVector2 of at least getNumVerts length
    void setTexCoord0Data(const LLVector2* data);

    // set color data via glBufferSubData
    // data must point to an array of LLColor4U of at least getNumVerts length
    void setColorData(const LLColor4U* data);

	// set for rendering
	void	setBuffer(U32 data_mask); 	// calls  setupVertexBuffer() if data_mask is not 0

	// allocate buffer
	bool	allocateBuffer(S32 nverts, S32 nindices, bool map);

	bool isEmpty() const					{ return mSize == 0; }
	S32 getNumVerts() const					{ return mNumVerts; }
	S32 getNumIndices() const				{ return mNumIndices; }
	
	U32 getTypeMask() const					{ return mTypeMask; }
	bool hasDataType(S32 type) const		{ return ((1 << type) & getTypeMask()); }
	S32 getSize() const;
	S32 getIndicesSize() const				{ return mIndicesSize; }
	S32 getOffset(S32 type) const			{ return mOffsets[type]; }
	S32 getUsage() const					{ return mUsage; }
	
	void draw(U32 mode, U32 count, U32 indices_offset) const;
	void drawArrays(U32 mode, U32 offset, U32 count) const;
	void drawRange(U32 mode, U32 start, U32 end, U32 count, U32 indices_offset) const;

    #ifdef LL_PROFILER_ENABLE_RENDER_DOC
	void setLabel(const char* label);
	#endif

protected:	
	U32		mGLBuffer;		// GL VBO handle
	U32		mGLIndices;		// GL IBO handle

    S32		mNumVerts;		// Number of vertices allocated
    S32		mNumIndices;	// Number of indices allocated
    S32		mSize; // size of mGLBuffer in bytes
    S32		mIndicesSize;  // size of mGLIndices in bytes

    U32		mTypeMask; // bitmask of attributes stored in mGLBuffer

    const S32		mUsage;			// GL usage (must be one of GL_STATIC_DRAW, GL_DYNAMIC_DRAW, or GL_STREAM_DRAW)
    
	S32		mOffsets[TYPE_MAX];

    LLMappedVertexData mMappedData;
    U16* mMappedIndices = nullptr;

    std::mutex mMutex;
    std::condition_variable mMapCondition;

    std::mutex mIBOMutex;
    std::condition_variable mIBOMapCondition;

    void* mVBOData = nullptr;  // pointer to mapped VBO data (write only)
    void* mIBOData = nullptr;  // pointer to mapped IBO data (write only)
    GLsync mSync = 0;
    GLsync mIBOSync = 0;

    bool mMappable = false;

    enum eState
    {
        STATE_INIT,
        STATE_EMPTY,
        STATE_MAPPED,
        STATE_UNMAPPED,
        STATE_READY
    };
    eState mState = STATE_INIT;
    eState mIndexState = STATE_INIT;

private:
	static LLPrivateMemoryPool* sPrivatePoolp;

public:
	typedef std::list<LLVertexBuffer*> buffer_list_t;
	
	static const S32 sTypeSize[TYPE_MAX];
	static const U32 sGLMode[LLRender::NUM_MODES];
	static U32 sGLRenderBuffer;
	static U32 sGLRenderIndices;
	static U32 sLastMask;
};

class LLVertexBufferThread : public LLSimpleton<LLVertexBufferThread>, LLGLThread
{
public:
    LLVertexBufferThread(LLWindow* window);
};

#ifdef LL_PROFILER_ENABLE_RENDER_DOC
#define LL_LABEL_VERTEX_BUFFER(buf, name) buf->setLabel(name)
#else
#define LL_LABEL_VERTEX_BUFFER(buf, name)
#endif


#endif // LL_LLVERTEXBUFFER_H
