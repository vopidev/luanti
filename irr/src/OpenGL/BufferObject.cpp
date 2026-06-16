// Copyright (C) 2024 sfan5
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "BufferObject.h"

#include <cassert>
#include <mt_opengl.h>

namespace video
{

void OGLBufferObject::upload(const void *data, size_t size, size_t offset,
		GLenum usage, bool mustShrink)
{
	bool newBuffer = false;
	assert(!(mustShrink && offset > 0)); // forbidden usage
	if (!m_name) {
		GL.GenBuffers(1, &m_name);
		if (!m_name)
			return;
		newBuffer = true;
	} else if (size > m_size || mustShrink) {
		newBuffer = size != m_size;
	}

	GL.BindBuffer(m_target, m_name);

	if (newBuffer) {
		assert(offset == 0);
		GL.BufferData(m_target, size, data, usage);
		m_size = size;
		m_usage = usage;
	} else {
#if defined(_IRR_IOS_PLATFORM_)
		// Buffer rename pattern: on Apple's GL→Metal translation layer
		// BufferSubData can stall the CPU on gldFinishObject while the
		// GPU is still using the buffer for a previous frame. For full
		// rewrites, calling BufferData with the same size hints to the
		// driver that the old contents are not needed — it hands us a
		// fresh allocation and frees the old one once the GPU is done.
		// Other GL drivers already implement buffer ghosting in hardware
		// so this is iOS-only.
		if (offset == 0 && size == m_size) {
			GL.BufferData(m_target, size, data, m_usage);
		} else {
			GL.BufferSubData(m_target, offset, size, data);
		}
#else
		GL.BufferSubData(m_target, offset, size, data);
#endif
	}

	GL.BindBuffer(m_target, 0);
}

void OGLBufferObject::destroy()
{
	if (m_name)
		GL.DeleteBuffers(1, &m_name);
	m_name = 0;
	m_size = 0;
}

}
