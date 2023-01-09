/** 
 * @file llvertexbuffer.cpp
 * @brief LLVertexBuffer implementation
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

#include "linden_common.h"

#include "llfasttimer.h"
#include "llsys.h"
#include "llvertexbuffer.h"
// #include "llrender.h"
#include "llglheaders.h"
#include "llrender.h"
#include "llvector4a.h"
#include "llshadermgr.h"
#include "llglslshader.h"
#include "llmemory.h"

#define USE_MAP_BUFFER 0

#define THREAD_COUNT 1
#define MAX_IMMEDIATE_BYTES 0xFFFFFFFF

//============================================================================

// High performance WorkQueue for usage in real-time rendering work
class GLWorkQueue
{
public:
    using Work = std::function<void()>;

    GLWorkQueue();

    void post(const Work& value);

    size_t size();

    bool done();

    // Get the next element from the queue
    Work pop();

    void runOne();

    bool runPending();

    void runUntilClose();

    void close();

    bool isClosed();

    void syncGL();

private:
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::queue<Work> mQueue;
    bool mClosed = false;
};

GLWorkQueue::GLWorkQueue()
{

}

void GLWorkQueue::syncGL()
{
    /*if (mSync)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        glWaitSync(mSync, 0, GL_TIMEOUT_IGNORED);
        mSync = 0;
    }*/
}

size_t GLWorkQueue::size()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue.size();
}

bool GLWorkQueue::done()
{
    return size() == 0 && isClosed();
}

void GLWorkQueue::post(const GLWorkQueue::Work& value)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mQueue.push(std::move(value));
    }

    mCondition.notify_one();
}

// Get the next element from the queue
GLWorkQueue::Work GLWorkQueue::pop()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    // Lock the mutex
    {
        std::unique_lock<std::mutex> lock(mMutex);

        // Wait for a new element to become available or for the queue to close
        {
            mCondition.wait(lock, [=] { return !mQueue.empty() || mClosed; });
        }
    }

    Work ret;

    {
        std::lock_guard<std::mutex> lock(mMutex);

        // Get the next element from the queue
        if (mQueue.size() > 0)
        {
            ret = mQueue.front();
            mQueue.pop();
        }
        else
        {
            ret = []() {};
        }
    }

    return ret;
}

void GLWorkQueue::runOne()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    Work w = pop();
    w();
    //mSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void GLWorkQueue::runUntilClose()
{
    while (!isClosed())
    {
        runOne();
    }
}

void GLWorkQueue::close()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mClosed = true;
    }

    mCondition.notify_all();
}

bool GLWorkQueue::isClosed()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    std::lock_guard<std::mutex> lock(mMutex);
    return mClosed;
}

#include "llwindow.h"

class LLGLWorkerThread : public LLThread
{
public:
    LLGLWorkerThread(const std::string& name, GLWorkQueue* queue, LLWindow* window)
        : LLThread(name)
    {
        mWindow = window;
        //mContext = mWindow->createSharedContext();
        mQueue = queue;
    }

    void run() override
    {
        //mWindow->makeContextCurrent(mContext);
        //gGL.init(false);
        mQueue->runUntilClose();
        //gGL.shutdown();
        //mWindow->destroySharedContext(mContext);
    }

    GLWorkQueue* mQueue;
    LLWindow* mWindow;
    void* mContext = nullptr;
};


static LLGLWorkerThread* sVBOThread[THREAD_COUNT];
static GLWorkQueue* sQueue = nullptr;

U32 LLVertexBuffer::sGLRenderBuffer = 0;
U32 LLVertexBuffer::sGLRenderIndices = 0;
U32 LLVertexBuffer::sLastMask = 0;

//NOTE: each component must be AT LEAST 4 bytes in size to avoid a performance penalty on AMD hardware
const S32 LLVertexBuffer::sTypeSize[LLVertexBuffer::TYPE_MAX] =
{
	sizeof(LLVector4), // TYPE_VERTEX,
	sizeof(LLVector4), // TYPE_NORMAL,
	sizeof(LLVector2), // TYPE_TEXCOORD0,
	sizeof(LLVector2), // TYPE_TEXCOORD1,
	sizeof(LLVector2), // TYPE_TEXCOORD2,
	sizeof(LLVector2), // TYPE_TEXCOORD3,
	sizeof(LLColor4U), // TYPE_COLOR,
	sizeof(LLColor4U), // TYPE_EMISSIVE, only alpha is used currently
	sizeof(LLVector4), // TYPE_TANGENT,
	sizeof(F32),	   // TYPE_WEIGHT,
	sizeof(LLVector4), // TYPE_WEIGHT4,
	sizeof(LLVector4), // TYPE_TEXTURE_INDEX (actually exists as position.w), no extra data, but stride is 16 bytes
};

static const std::string vb_type_name[] =
{
	"TYPE_VERTEX",
	"TYPE_NORMAL",
	"TYPE_TEXCOORD0",
	"TYPE_TEXCOORD1",
	"TYPE_TEXCOORD2",
	"TYPE_TEXCOORD3",
	"TYPE_COLOR",
	"TYPE_EMISSIVE",
	"TYPE_TANGENT",
	"TYPE_WEIGHT",
	"TYPE_WEIGHT4",
	"TYPE_TEXTURE_INDEX",
	"TYPE_MAX",
	"TYPE_INDEX",	
};

const U32 LLVertexBuffer::sGLMode[LLRender::NUM_MODES] =
{
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_QUADS,
	GL_LINE_LOOP,
};

//static
void LLVertexBuffer::setupClientArrays(U32 data_mask)
{
	if (sLastMask != data_mask)
	{
		for (U32 i = 0; i < TYPE_MAX; ++i)
		{
			S32 loc = i;
										
			U32 mask = 1 << i;

			if (sLastMask & (1 << i))
			{ //was enabled
				if (!(data_mask & mask))
				{ //needs to be disabled
					glDisableVertexAttribArray(loc);
				}
			}
			else 
			{	//was disabled
				if (data_mask & mask)
				{ //needs to be enabled
					glEnableVertexAttribArray(loc);
				}
			}
		}
				
		sLastMask = data_mask;
	}
}

//static
void LLVertexBuffer::drawArrays(U32 mode, const std::vector<LLVector3>& pos)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
    gGL.begin(mode);
    for (auto& v : pos)
    {
        gGL.vertex3fv(v.mV);
    }
    gGL.end();
    gGL.flush();
}

//static
void LLVertexBuffer::drawElements(U32 mode, const LLVector4a* pos, const LLVector2* tc, S32 num_indices, const U16* indicesp)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	llassert(LLGLSLShader::sCurBoundShaderPtr != NULL);

	gGL.syncMatrices();

	U32 mask = LLVertexBuffer::MAP_VERTEX;
	if (tc)
	{
		mask = mask | LLVertexBuffer::MAP_TEXCOORD0;
	}

	unbind();
	
    gGL.begin(mode);

    if (tc != nullptr)
    {
        for (int i = 0; i < num_indices; ++i)
        {
            U16 idx = indicesp[i];
            gGL.texCoord2fv(tc[idx].mV);
            gGL.vertex3fv(pos[idx].getF32ptr());
        }
    }
    else
    {
        for (int i = 0; i < num_indices; ++i)
        {
            U16 idx = indicesp[i];
            gGL.vertex3fv(pos[idx].getF32ptr());
        }
    }
    
    gGL.end();
    gGL.flush();
}

#ifdef LL_PROFILER_ENABLE_RENDER_DOC
void LLVertexBuffer::setLabel(const char* label) {
	LL_LABEL_OBJECT_GL(GL_BUFFER, mGLBuffer, strlen(label), label);
}
#endif

void LLVertexBuffer::drawRange(U32 mode, U32 start, U32 end, U32 count, U32 indices_offset) const
{
	gGL.syncMatrices();
	U16* idx = ((U16*) nullptr)+indices_offset;

	glDrawRangeElements(sGLMode[mode], start, end, count, GL_UNSIGNED_SHORT, 
		idx);
}

void LLVertexBuffer::draw(U32 mode, U32 count, U32 indices_offset) const
{
	gGL.syncMatrices();

    glDrawElements(sGLMode[mode], count, GL_UNSIGNED_SHORT,
		((U16*) nullptr) + indices_offset);

	stop_glerror();
}


void LLVertexBuffer::drawArrays(U32 mode, U32 first, U32 count) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

    gGL.syncMatrices();

    glDrawArrays(sGLMode[mode], first, count);
}

//static
void LLVertexBuffer::initClass(LLWindow* window)
{
   //LLVertexBufferThread::createInstance(window);
   sQueue = new GLWorkQueue();

   for (int i = 0; i < THREAD_COUNT; ++i)
   {
       sVBOThread[i] = new LLGLWorkerThread("VBO Worker", sQueue, window);
       sVBOThread[i]->start();
   }
}

//static
void LLVertexBuffer::cleanupClass()
{
    unbind();
    //LLVertexBufferThread::deleteSingleton();
}

//static 
void LLVertexBuffer::unbind()
{
	if (sGLRenderBuffer)
	{
        glBindBuffer(GL_ARRAY_BUFFER, 0);
		
	}

    if (sGLRenderIndices)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
	
	sGLRenderBuffer = 0;
	sGLRenderIndices = 0;
}


//----------------------------------------------------------------------------
LLVertexBuffer::LLVertexBuffer(U32 typemask, S32 usage) 
:	LLRefCount(),

	mNumVerts(0),
	mNumIndices(0),
	mSize(0),
	mIndicesSize(0),
	mTypeMask(typemask),
	mUsage(usage),
	mGLBuffer(0),
	mGLIndices(0)
{
    llassert(usage == GL_STREAM_DRAW
        || usage == GL_DYNAMIC_DRAW
        || usage == GL_STATIC_DRAW);

	//zero out offsets
	for (U32 i = 0; i < TYPE_MAX; i++)
	{
		mOffsets[i] = 0;
	}
}

//static
S32 LLVertexBuffer::calcOffsets(const U32& typemask, S32* offsets, S32 num_vertices)
{
	S32 offset = 0;
	for (S32 i=0; i<TYPE_TEXTURE_INDEX; i++)
	{
		U32 mask = 1<<i;
		if (typemask & mask)
		{
			if (offsets && LLVertexBuffer::sTypeSize[i])
			{
				offsets[i] = offset;
				offset += LLVertexBuffer::sTypeSize[i]*num_vertices;
				offset = (offset + 0xF) & ~0xF;
			}
		}
	}

	offsets[TYPE_TEXTURE_INDEX] = offsets[TYPE_VERTEX] + 12;
	
	return offset+16;
}

//static 
S32 LLVertexBuffer::calcVertexSize(const U32& typemask)
{
	S32 size = 0;
	for (S32 i = 0; i < TYPE_TEXTURE_INDEX; i++)
	{
		U32 mask = 1<<i;
		if (typemask & mask)
		{
			size += LLVertexBuffer::sTypeSize[i];
		}
	}

	return size;
}

S32 LLVertexBuffer::getSize() const
{
	return mSize;
}

// ll_aligned_free_16 ptr on a background thread
static void jetison(void* ptr)
{
    sQueue->post([=] { ll_aligned_free_16(ptr); });
}

// protected, use unref()
//virtual
LLVertexBuffer::~LLVertexBuffer()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

    if (mMappable)
    {
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mMapCondition.wait(lk, [this] { return mState == STATE_READY; });
        }

        if (mIndexState != STATE_INIT)
        {
            std::unique_lock<std::mutex> lk(mIBOMutex);
            mIBOMapCondition.wait(lk, [this] { return mIndexState == STATE_READY; });
        }
    }

	destroyGLBuffer();
	destroyGLIndices();

    if (mMappedData.mPosition)
    {
        jetison(mMappedData.mPosition);
    }

    if (mMappedIndices)
    {
        ll_aligned_free_16(mMappedIndices);
    }
};


// expected usage:
// vb = new LLVertexBuffer(...)         STATE_INIT
// vb->allocateBuffer(...)              STATE_EMPTY
// vb->mapVertexData()                  STATE_MAPPED
// vb->mapIndexData()
// ...
// vb->unmapVertexData()                STATE_UNMAPPED
// vb->unmapIndexData()
// ...
// vb->setBuffer(...)
// vb->draw
//----------------------------------------------------------------------------

// batch glGenBuffers
static GLuint gen_buffer()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
    constexpr U32 pool_size = 4096;

    thread_local static GLuint sNamePool[pool_size];
    thread_local static U32 sIndex = 0;

    if (sIndex == 0)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("gen ibo");
        sIndex = pool_size;
        glGenBuffers(pool_size, sNamePool);
    }

    return sNamePool[--sIndex];
}

// batch glDeleteBuffers
static void release_buffer(U32 buff)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
#if 0
    
    constexpr U32 pool_size = 4096;

    thread_local static GLuint sNamePool[pool_size];
    thread_local static U32 sIndex = 0;

    if (sIndex == pool_size)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("gen ibo");
        sIndex = 0;
        glDeleteBuffers(pool_size, sNamePool);
    }

    sNamePool[sIndex++] = buff;
#else
    glDeleteBuffers(1, &buff);
#endif
}

void LLVertexBuffer::genBuffer(U32 size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
    mSize = size;

    llassert(mState == STATE_INIT);
    mState = STATE_EMPTY;


    if (mMappable)
    {
#if 1
        if (mSize > MAX_IMMEDIATE_BYTES)
        {
            sQueue->post(
                [this]()
                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("genBuffer work");
                    llassert(mState < STATE_READY);

                    mGLBuffer = gen_buffer();

                    {
                        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("genBuffer bind");
                        glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
                    }
                
                    {
                        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("genBuffer alloc");
                        glBufferData(GL_ARRAY_BUFFER, mSize, nullptr, GL_STATIC_DRAW);
                    }

                    void* md = nullptr;
                
                    {
                        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("genBuffer map");
                        md = glMapBufferRange(GL_ARRAY_BUFFER, 0, mSize, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
                    }

                    {
                        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("genBuffer unbind");
                        glBindBuffer(GL_ARRAY_BUFFER, 0);
                    }

                    {
                        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("genBuffer notify");
                        {
                            std::lock_guard<std::mutex> lk(mMutex);
                            mVBOData = md;
                        }
                        mMapCondition.notify_all();
                    }
                });
        }
#endif
    }
    else
    {
        mGLBuffer = gen_buffer();
        bindGLBuffer();
        glBufferData(GL_ARRAY_BUFFER, mSize, nullptr, GL_STATIC_DRAW);
    }
 }

LLMappedVertexData LLVertexBuffer::mapVertexBuffer()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
    llassert(mMappable);
    llassert(mState == STATE_EMPTY);
    mState = STATE_MAPPED;

    U8* base = (U8*)(mMappedData.mPosition ? mMappedData.mPosition : ll_aligned_malloc_16(mSize));

    LLMappedVertexData& md = mMappedData;

    if (base)
    {
        md.mPosition = (LLVector4a*)base;
        md.mNormal = (LLVector4a*)(mTypeMask & MAP_NORMAL ? base + mOffsets[TYPE_NORMAL] : nullptr);
        md.mTexCoord0 = (LLVector2*)(mTypeMask & MAP_TEXCOORD0 ? base + mOffsets[TYPE_TEXCOORD0] : nullptr);
        md.mTexCoord1 = (LLVector2*)(mTypeMask & MAP_TEXCOORD1 ? base + mOffsets[TYPE_TEXCOORD1] : nullptr);
        md.mTexCoord2 = (LLVector2*)(mTypeMask & MAP_TEXCOORD2 ? base + mOffsets[TYPE_TEXCOORD2] : nullptr);
        md.mTexCoord3 = (LLVector2*)(mTypeMask & MAP_TEXCOORD3 ? base + mOffsets[TYPE_TEXCOORD3] : nullptr);
        md.mColor = (LLColor4U*)(mTypeMask & MAP_COLOR ? base + mOffsets[TYPE_COLOR] : nullptr);
        md.mEmissive = (LLColor4U*)(mTypeMask & MAP_EMISSIVE ? base + mOffsets[TYPE_EMISSIVE] : nullptr);
        md.mTangent = (LLVector4a*)(mTypeMask & MAP_TANGENT ? base + mOffsets[TYPE_TANGENT] : nullptr);
        md.mWeight = (F32*)(mTypeMask & MAP_WEIGHT ? base + mOffsets[TYPE_WEIGHT] : nullptr);
        md.mWeight4 = (LLVector4a*)(mTypeMask & MAP_WEIGHT4 ? base + mOffsets[TYPE_WEIGHT4] : nullptr);
    }

    return mMappedData;
}

void LLVertexBuffer::unmapVertexBuffer()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
    llassert(mMappedData.mPosition); // not mapped
    llassert(mMappable);

    llassert(mState == STATE_MAPPED);
    mState = STATE_UNMAPPED;

    if (mSize > MAX_IMMEDIATE_BYTES)
    {  // large buffer, shuttle to background thread
#if 1
        sQueue->post([this]()
            {
                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo map wait");
                    std::unique_lock<std::mutex> lk(mMutex);
                    mMapCondition.wait(lk, [this] { return mVBOData != nullptr; });
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo copy");
                    ll_memcpy_nonaliased_aligned_16((char*)mVBOData, (char*)mMappedData.mPosition, mSize);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo unmap bind");
                    glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("glFlushMappedBufferRange(GL_ARRAY_BUFFER)");
                    glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, mSize);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("glUnmapBuffer(GL_ARRAY_BUFFER)");
                    glUnmapBuffer(GL_ARRAY_BUFFER);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    {
                        std::lock_guard<std::mutex> lk(mMutex);
                        mState = STATE_READY;
                    }
                    mMapCondition.notify_all();
                }
            });
#else
        sQueue->post([this]()
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo create");
                mGLBuffer = gen_buffer();

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo copy");
                    glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
                    glBufferData(GL_ARRAY_BUFFER, mSize, mMappedData.mPosition, GL_STATIC_DRAW);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo sync");
                    mSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                }

                {
                    std::lock_guard<std::mutex> lk(mMutex);
                    mState = STATE_READY;
                }
                mMapCondition.notify_all();
            });
#endif
    }
    else
    { //small enough to just do inline on main thread
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo create 2");
        mGLBuffer = gen_buffer();
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo copy 2");
            LL_PROFILE_ZONE_NUM(mSize);
            glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
            glBufferData(GL_ARRAY_BUFFER, mSize, mMappedData.mPosition, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        mState = STATE_READY;
    }
}

void LLVertexBuffer::bindGLBuffer()
{
    if (mMappable && mState != STATE_READY)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo bind wait");
        std::unique_lock<std::mutex> lk(mMutex);
        mMapCondition.wait(lk, [this] { return mState == STATE_READY; });
    }

#if 0
    if (mSync)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo bind sync");
        glWaitSync(mSync, 0, GL_TIMEOUT_IGNORED);
        mSync = 0;
    }
#endif

    if (mGLBuffer != sGLRenderBuffer)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo bind");
        glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
        sGLRenderBuffer = mGLBuffer;
    }
}

void LLVertexBuffer::genIndices(U32 size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

    mIndicesSize = size;

    llassert(mIndexState == STATE_INIT);
    mIndexState = STATE_EMPTY;

    if (mIndicesSize > MAX_IMMEDIATE_BYTES)
    {
#if 1
        sQueue->post([this]()
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("genIndices work");
                llassert(mIndexState < STATE_READY);

                mGLIndices = gen_buffer();
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, mIndicesSize, nullptr, GL_STATIC_DRAW);
                void* md = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, mIndicesSize, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                {
                    std::lock_guard<std::mutex> lk(mIBOMutex);
                    mIBOData = md;
                }
                mIBOMapCondition.notify_all();

            });
#endif
    }
}

U16* LLVertexBuffer::mapIndexBuffer()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
    llassert(mIndexState == STATE_EMPTY);
    mIndexState = STATE_MAPPED;

    if (!mMappedIndices)
    {
        mMappedIndices = (U16*)ll_aligned_malloc_16(mIndicesSize);
    }

    llassert(mMappedIndices);
    return mMappedIndices;
}

void LLVertexBuffer::unmapIndexBuffer()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

    llassert(mMappedIndices); // not mapped

    llassert(mIndexState == STATE_MAPPED);
    mIndexState = STATE_UNMAPPED;
    
    if (mIndicesSize > MAX_IMMEDIATE_BYTES)
    {
#if 1
        sQueue->post([this]()
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo unmap wait");
                {
                    std::unique_lock<std::mutex> lk(mIBOMutex);
                    mIBOMapCondition.wait(lk, [this] { return mIBOData != nullptr; });
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo copy");
                    ll_memcpy_nonaliased_aligned_16((char*)mIBOData, (char*)mMappedIndices, mIndicesSize);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo unmap bind");
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("glFlushMappedBufferRange(GL_ELEMENT_ARRAY_BUFFER)");
                    glFlushMappedBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, mIndicesSize);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER)");
                    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
                }

                {
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                }


                {
                    std::lock_guard<std::mutex> lk(mIBOMutex);
                    mIndexState = STATE_READY;
                }
                mIBOMapCondition.notify_all();
            });
#else
        sQueue->post([this]()
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo create");
                mGLIndices = gen_buffer();

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo copy");
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mIndicesSize, mMappedIndices, GL_STATIC_DRAW);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo sync");
                    mIBOSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                }

                {
                    std::lock_guard<std::mutex> lk(mIBOMutex);
                    mIndexState = STATE_READY;
                }
                mIBOMapCondition.notify_all();
            });
#endif
    }
    else
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo create 2");
        mGLIndices = gen_buffer();

        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo copy 2");
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mIndicesSize, mMappedIndices, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }

        mIndexState = STATE_READY;
    }
}
    

bool LLVertexBuffer::isMappable() const
{
    return mState == STATE_EMPTY;
}


void LLVertexBuffer::bindGLIndices()
{
    if (mIndexState != STATE_READY)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo bind wait");
        std::unique_lock<std::mutex> lk(mIBOMutex);
        mIBOMapCondition.wait(lk, [this] { return mIndexState == STATE_READY; });
    }

#if 0
    if (mIBOSync)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("ibo bind sync");
        glWaitSync(mIBOSync, 0, GL_TIMEOUT_IGNORED);
        mIBOSync = 0;
    }
#endif

    if (mGLIndices != sGLRenderIndices)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
        sGLRenderIndices = mGLIndices;
    }
}

void LLVertexBuffer::releaseBuffer()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

    release_buffer(mGLBuffer);
    //glDeleteBuffers(1, &mGLBuffer);
    
	mGLBuffer = 0;
}

void LLVertexBuffer::releaseIndices()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

    release_buffer(mGLIndices);

	mGLIndices = 0;
}

bool LLVertexBuffer::createGLBuffer(U32 size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	if (mGLBuffer)
	{
		destroyGLBuffer();
	}

	if (size == 0)
	{
		return true;
	}

	bool success = true;
	
	genBuffer(size);

	return success;
}

bool LLVertexBuffer::createGLIndices(U32 size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	if (mGLIndices)
	{
		destroyGLIndices();
	}
	
	if (size == 0)
	{
		return true;
	}

	bool success = true;

	//pad by 16 bytes for aligned copies
	size += 16;

	genIndices(size);

	return success;
}

void LLVertexBuffer::destroyGLBuffer()
{
	if (mGLBuffer)
	{
		releaseBuffer();
	}
	
	mGLBuffer = 0;
}

void LLVertexBuffer::destroyGLIndices()
{
	if (mGLIndices)
	{
		releaseIndices();
	}

    mGLIndices = 0;
}

bool LLVertexBuffer::updateNumVerts(S32 nverts)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
    llassert(mMappedData.mPosition == nullptr);

	llassert(nverts >= 0);

	bool success = true;

	if (nverts > 65536)
	{
		LL_WARNS() << "Vertex buffer overflow!" << LL_ENDL;
		nverts = 65536;
	}

	U32 needed_size = calcOffsets(mTypeMask, mOffsets, nverts);

	if (needed_size > mSize || needed_size <= mSize/2)
	{
		success &= createGLBuffer(needed_size);
	}

	mNumVerts = nverts;

	return success;
}

bool LLVertexBuffer::updateNumIndices(S32 nindices)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	llassert(nindices >= 0);
    llassert(mMappedIndices == nullptr);

	bool success = true;

	U32 needed_size = sizeof(U16) * nindices;

	success = createGLIndices(needed_size);

	mNumIndices = nindices;

	return success;
}

bool LLVertexBuffer::allocateBuffer(S32 nverts, S32 nindices, bool map)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	stop_glerror();

	if (nverts < 0 || nindices < 0 ||
		nverts > 65536)
	{
		LL_ERRS() << "Bad vertex buffer allocation: " << nverts << " : " << nindices << LL_ENDL;
	}

	bool success = true;

    mMappable = map;

	success &= updateNumVerts(nverts);
	success &= updateNumIndices(nindices);
	
	return success;
}

//----------------------------------------------------------------------------

bool expand_region(LLVertexBuffer::MappedRegion& region, S32 index, S32 count)
{
	S32 end = index+count;
	S32 region_end = region.mIndex+region.mCount;
	
	if (end < region.mIndex ||
		index > region_end)
	{ //gap exists, do not merge
		return false;
	}

	S32 new_end = llmax(end, region_end);
	S32 new_index = llmin(index, region.mIndex);
	region.mIndex = new_index;
	region.mCount = new_end-new_index;
	return true;
}


void LLVertexBuffer::setPositionData(const LLVector4a* data)
{
    llassert(!mMappable);
    bindGLBuffer();
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(LLVector4a) * getNumVerts(), data);
}

void LLVertexBuffer::setTexCoord0Data(const LLVector2* data)
{
    llassert(!mMappable);
    bindGLBuffer();
    glBufferSubData(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD0], sizeof(LLVector2) * getNumVerts(), data);
}

void LLVertexBuffer::setColorData(const LLColor4U* data)
{
    llassert(!mMappable);
    bindGLBuffer();
    glBufferSubData(GL_ARRAY_BUFFER, mOffsets[TYPE_COLOR], sizeof(LLColor4U) * getNumVerts(), data);
}


// Set for rendering
void LLVertexBuffer::setBuffer(U32 data_mask)
{
    //set up pointers if the data mask is different or render buffer is changing
    bool setup = (sLastMask != data_mask) || mGLBuffer != sGLRenderBuffer;

    bindGLBuffer();

    if (mIndexState != STATE_INIT)
    { // only bind index buffer if we've attempted to create one for this LLVertexBuffer
        bindGLIndices();
    }

    if (mGLBuffer)
    {
        if (data_mask && setup)
        {
            setupVertexBuffer(data_mask); // subclass specific setup (virtual function)
        }
    }
}

// virtual (default)
void LLVertexBuffer::setupVertexBuffer(U32 data_mask)
{
    if (data_mask & MAP_NORMAL)
    {
        S32 loc = TYPE_NORMAL;
        void* ptr = (void*) ((U8*) nullptr + mOffsets[TYPE_NORMAL]);
        glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_NORMAL], ptr);
    }
    if (data_mask & MAP_TEXCOORD3)
    {
        S32 loc = TYPE_TEXCOORD3;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_TEXCOORD3]);
        glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TEXCOORD3], ptr);
    }
    if (data_mask & MAP_TEXCOORD2)
    {
        S32 loc = TYPE_TEXCOORD2;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_TEXCOORD2]);
        glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TEXCOORD2], ptr);
    }
    if (data_mask & MAP_TEXCOORD1)
    {
        S32 loc = TYPE_TEXCOORD1;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_TEXCOORD1]);
        glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TEXCOORD1], ptr);
    }
    if (data_mask & MAP_TANGENT)
    {
        S32 loc = TYPE_TANGENT;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_TANGENT]);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TANGENT], ptr);
    }
    if (data_mask & MAP_TEXCOORD0)
    {
        S32 loc = TYPE_TEXCOORD0;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_TEXCOORD0]);
        glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TEXCOORD0], ptr);
    }
    if (data_mask & MAP_COLOR)
    {
        S32 loc = TYPE_COLOR;
        //bind emissive instead of color pointer if emissive is present
        void* ptr = (data_mask & MAP_EMISSIVE) ? (void*)((U8*) nullptr + mOffsets[TYPE_EMISSIVE]) : (void*)((U8*) nullptr + mOffsets[TYPE_COLOR]);
        glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, LLVertexBuffer::sTypeSize[TYPE_COLOR], ptr);
    }
    if (data_mask & MAP_EMISSIVE)
    {
        S32 loc = TYPE_EMISSIVE;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_EMISSIVE]);
        glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, LLVertexBuffer::sTypeSize[TYPE_EMISSIVE], ptr);

        if (!(data_mask & MAP_COLOR))
        { //map emissive to color channel when color is not also being bound to avoid unnecessary shader swaps
            loc = TYPE_COLOR;
            glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, LLVertexBuffer::sTypeSize[TYPE_EMISSIVE], ptr);
        }
    }
    if (data_mask & MAP_WEIGHT)
    {
        S32 loc = TYPE_WEIGHT;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_WEIGHT]);
        glVertexAttribPointer(loc, 1, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_WEIGHT], ptr);
    }
    if (data_mask & MAP_WEIGHT4)
    {
        S32 loc = TYPE_WEIGHT4;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_WEIGHT4]);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_WEIGHT4], ptr);
    }
    if (data_mask & MAP_TEXTURE_INDEX)
    {
        S32 loc = TYPE_TEXTURE_INDEX;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_VERTEX] + 12);
        glVertexAttribIPointer(loc, 1, GL_UNSIGNED_INT, LLVertexBuffer::sTypeSize[TYPE_VERTEX], ptr);
    }
    if (data_mask & MAP_VERTEX)
    {
        S32 loc = TYPE_VERTEX;
        void* ptr = (void*)((U8*) nullptr + mOffsets[TYPE_VERTEX]);
        glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_VERTEX], ptr);
    }
}

LLVertexBuffer::MappedRegion::MappedRegion(S32 type, S32 index, S32 count)
: mType(type), mIndex(index), mCount(count)
{ 
	mEnd = mIndex+mCount;	
}	

LLVertexBufferThread::LLVertexBufferThread(LLWindow* window)
// We want exactly one thread.
    : LLGLThread(window, "LLVertexBuffer")
{

}


