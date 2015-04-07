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
 * Copyright (c) 2007 The Zdeno Ash Miklas
 *
 * This source file is part of VideoTexture library
 *
 * Contributor(s):
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file VideoDeckLink.h
 *  \ingroup bgevideotex
 */

#ifndef __VIDEODECKLINK_H__
#define __VIDEODECKLINK_H__

#ifdef WITH_DECKLINK

/* this needs to be parsed with __cplusplus defined before included through DeckLink_compat.h */
#if defined(__FreeBSD__)
#  include <inttypes.h>
#endif
#include <map>
#include <set>

extern "C" {
#include <pthread.h>
#include "DNA_listBase.h"
#include "BLI_threads.h"
#include "BLI_blenlib.h"
}
#include "GL/glew.h"
#ifdef WIN32
#include "dvpapi.h"
#endif
#include "DeckLinkAPI.h"
#include "VideoBase.h"

class PinnedMemoryAllocator;

struct TextureDesc
{
	u_int		width;
	u_int		height;
	u_int		stride;
	GLenum		format;
	GLenum		type;
	TextureDesc()
	{
		width = 0;
		height = 0;
		stride = 0;
		format = 0;
		type = 0;
	}
};

// type VideoDeckLink declaration
class VideoDeckLink : public VideoBase
{
	friend class CaptureDelegate;
public:
	/// constructor
	VideoDeckLink (HRESULT * hRslt);
	/// destructor
	virtual ~VideoDeckLink ();

	/// open video/image file
	virtual void openFile(char *file);
	/// open video capture device
	virtual void openCam(char *driver, short camIdx);

	/// release video source
	virtual bool release (void);
    /// overwrite base refresh to handle fixed image
    virtual void refresh(void);
	/// play video
	virtual bool play (void);
	/// pause video
	virtual bool pause (void);
	/// stop video
	virtual bool stop (void);
	/// set play range
	virtual void setRange (double start, double stop);
	/// set frame rate
	virtual void setFrameRate (float rate);

protected:
	// format and codec information
	/// image calculation
	virtual void calcImage (unsigned int texId, double ts);

private:
	void					VideoFrameArrived(IDeckLinkVideoInputFrame* inputFrame);
	void					LockCache()
	{
		pthread_mutex_lock(&mCacheMutex);
	}
	void					UnlockCache()
	{
		pthread_mutex_unlock(&mCacheMutex);
	}

	IDeckLinkInput*			mDLInput;
	BMDDisplayMode			mDisplayMode;
	BMDPixelFormat			mPixelFormat;
	bool					mUse3D;
	u_int					mFrameWidth;
	u_int					mFrameHeight;
	TextureDesc				mTextureDesc;
	PinnedMemoryAllocator*	mpAllocator;
	CaptureDelegate*		mpCaptureDelegate;

	// cache frame in transit between the callback thread and the main BGE thread
	// keep only one frame in cache because we just want to keep up with real time
	pthread_mutex_t			mCacheMutex;
	IDeckLinkVideoInputFrame* mpCacheFrame;
	bool					mClosing;

};

inline VideoDeckLink *getDeckLink(PyImage *self)
{
	return static_cast<VideoDeckLink*>(self->m_image);
}

////////////////////////////////////////////
// TextureTransfer : Abstract class to perform a transfer to GPU memory using fast transfer if available
////////////////////////////////////////////
class TextureTransfer
{
public:
	TextureTransfer() {}
	virtual ~TextureTransfer() { }

	virtual void PerformTransfer() = 0;
};

////////////////////////////////////////////
// PinnedMemoryAllocator
////////////////////////////////////////////

// PinnedMemoryAllocator implements the IDeckLinkMemoryAllocator interface and can be used instead of the
// built-in frame allocator, by setting with SetVideoInputFrameMemoryAllocator() or SetVideoOutputFrameMemoryAllocator().
//
// For this sample application a custom frame memory allocator is used to ensure each address
// of frame memory is aligned on a 4kB boundary required by the OpenGL pinned memory extension.
// If the pinned memory extension is not available, this allocator will still be used and
// demonstrates how to cache frame allocations for efficiency.
//
// The frame cache delays the releasing of buffers until the cache fills up, thereby avoiding an
// allocate plus pin operation for every frame, followed by an unpin and deallocate on every frame.


class PinnedMemoryAllocator : public IDeckLinkMemoryAllocator
{
public:
	PinnedMemoryAllocator(unsigned cacheSize, size_t memSize);
	virtual ~PinnedMemoryAllocator();

	void TransferBuffer(void* address, TextureDesc* texDesc, GLuint texId);

	// IUnknown methods
	virtual HRESULT STDMETHODCALLTYPE	QueryInterface(REFIID iid, LPVOID *ppv);
	virtual ULONG STDMETHODCALLTYPE		AddRef(void);
	virtual ULONG STDMETHODCALLTYPE		Release(void);

	// IDeckLinkMemoryAllocator methods
	virtual HRESULT STDMETHODCALLTYPE	AllocateBuffer(unsigned long bufferSize, void* *allocatedBuffer);
	virtual HRESULT STDMETHODCALLTYPE	ReleaseBuffer(void* buffer);
	virtual HRESULT STDMETHODCALLTYPE	Commit();
	virtual HRESULT STDMETHODCALLTYPE	Decommit();

private:
	static bool				mGPUDirectInitialized;
	static bool				mHasDvp;
	static bool				mHasAMDPinnedMemory;
	static size_t			mReservedProcessMemory;
	static bool ReserveMemory(size_t size);

	void Lock()
	{
		pthread_mutex_lock(&mMutex);
	}
	void Unlock()
	{
		pthread_mutex_unlock(&mMutex);
	}
	void _UnpinBuffer(void* address, u_int size);
	bool _PinBuffer(void *address, u_int size);
	HRESULT _ReleaseBuffer(void* buffer);

	int									mRefCount;
	// protect the cache and the allocated map, 
	// not the pinnedBuffer map as it is only used from main thread
	pthread_mutex_t						mMutex;
	std::map<void*, u_int>				mAllocatedSize;
	std::vector<void*>					mBufferCache;
	u_int								mBufferCacheSize;
	std::map<void *, TextureTransfer*>	mPinnedBuffer;
	GLuint								mUnpinnedTextureBuffer;
#ifdef WIN32
	DVPBufferHandle						mDvpCaptureTextureHandle;
#endif
};

////////////////////////////////////////////
// Capture Delegate Class
////////////////////////////////////////////

class CaptureDelegate : public IDeckLinkInputCallback
{
	VideoDeckLink*	mpOwner;

public:
	CaptureDelegate(VideoDeckLink* pOwner);

	// IUnknown needs only a dummy implementation
	virtual HRESULT	STDMETHODCALLTYPE	QueryInterface(REFIID iid, LPVOID *ppv)	{ return E_NOINTERFACE; }
	virtual ULONG	STDMETHODCALLTYPE	AddRef()								{ return 1; }
	virtual ULONG	STDMETHODCALLTYPE	Release()								{ return 1; }

	virtual HRESULT STDMETHODCALLTYPE	VideoInputFrameArrived(IDeckLinkVideoInputFrame *videoFrame, IDeckLinkAudioInputPacket *audioPacket);
	virtual HRESULT	STDMETHODCALLTYPE	VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode *newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags);
};


#endif	/* WITH_DECKLINK */

#endif  /* __VIDEODECKLINK_H__ */
