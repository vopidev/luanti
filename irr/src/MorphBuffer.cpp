// Copyright (C) 2025 VOPI Team
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "MorphBuffer.h"

#include <algorithm>
#include <cmath>

namespace scene
{

void MorphBuffer::WeightChannel::sample(f32 frame, std::size_t numTargets,
		std::vector<f32> &out) const
{
	out.assign(numTargets, 0.0f);
	if (times.empty())
		return;

	// First keyframe with time >= frame.
	const auto next = std::lower_bound(times.begin(), times.end(), frame);

	auto copyFrame = [&](std::size_t f) {
		for (std::size_t t = 0; t < numTargets; ++t)
			out[t] = values[f * numTargets + t];
	};

	if (next == times.begin()) {
		copyFrame(0);
		return;
	}
	if (next == times.end()) {
		copyFrame(times.size() - 1);
		return;
	}

	const std::size_t i1 = static_cast<std::size_t>(std::distance(times.begin(), next));
	const std::size_t i0 = i1 - 1;
	if (!interpolate) {
		copyFrame(i0);
		return;
	}

	const f32 t0 = times[i0];
	const f32 t1 = times[i1];
	const f32 a = (t1 > t0) ? (frame - t0) / (t1 - t0) : 0.0f;
	for (std::size_t t = 0; t < numTargets; ++t) {
		const f32 v0 = values[i0 * numTargets + t];
		const f32 v1 = values[i1 * numTargets + t];
		out[t] = v0 + (v1 - v0) * a;
	}
}

f32 MorphBuffer::maxDisplacement() const
{
	if (targets.empty())
		return 0.0f;
	const std::size_t n = targets[0].positions.size();
	f32 maxd = 0.0f;
	for (std::size_t v = 0; v < n; ++v) {
		f32 sum = 0.0f;
		for (const auto &t : targets) {
			if (v < t.positions.size())
				sum += t.positions[v].getLength();
		}
		if (sum > maxd)
			maxd = sum;
	}
	return maxd;
}

f32 MorphBuffer::maxAbsWeight() const
{
	f32 m = 0.0f;
	for (f32 w : baseWeights)
		m = std::max(m, std::fabs(w));
	for (f32 w : channel.values)
		m = std::max(m, std::fabs(w));
	return m;
}

bool MorphBuffer::hasNormalDeltas() const
{
	for (const auto &t : targets) {
		if (t.normals)
			return true;
	}
	return false;
}

void MorphBuffer::computeMorphedVertex(std::size_t v, const std::vector<f32> &weights,
		bool doNormals, const core::vector3df &restPos,
		const core::vector3df &restNormal, core::vector3df &outPos,
		core::vector3df &outNormal) const
{
	outPos = restPos;
	core::vector3df nrm = restNormal;
	for (std::size_t t = 0; t < targets.size(); ++t) {
		const f32 wt = (t < weights.size()) ? weights[t] : 0.0f;
		if (wt == 0.0f)
			continue;
		outPos += targets[t].positions[v] * wt;
		if (doNormals && targets[t].normals)
			nrm += (*targets[t].normals)[v] * wt;
	}
	if (doNormals) {
		// If the morph deltas collapse the normal to ~zero, keep the rest normal
		// rather than emitting a zero/garbage normal that would break lighting.
		if (nrm.getLengthSQ() > 1e-12f)
			outNormal = nrm.normalize();
		else
			outNormal = restNormal;
	}
}

void MorphBuffer::captureRest(const IVertexBuffer *vbuf)
{
	const u32 n = vbuf->getCount();
	m_rest = std::make_unique<RestVertex[]>(n);
	for (u32 v = 0; v < n; ++v) {
		m_rest[v].pos = vbuf->getPosition(v);
		m_rest[v].normal = vbuf->getNormal(v);
	}
	m_rest_count = n;
	m_morph_normals = hasNormalDeltas();
}

void MorphBuffer::apply(IVertexBuffer *vbuf, f32 frame)
{
	const u32 n = vbuf->getCount();
	if (m_rest_count != n)
		captureRest(vbuf);

	// apply() is only invoked on animated buffers (morphMesh filters on
	// hasAnimation()); sample() also yields zero weights for an empty channel,
	// so sampling unconditionally is safe.
	channel.sample(frame, numTargets(), m_weights);

	for (u32 v = 0; v < n; ++v) {
		core::vector3df pos, nrm;
		computeMorphedVertex(v, m_weights, m_morph_normals,
				m_rest[v].pos, m_rest[v].normal, pos, nrm);
		vbuf->getPosition(v) = pos;
		if (m_morph_normals)
			vbuf->getNormal(v) = nrm;
	}
	vbuf->setDirty();
}

void MorphBuffer::bakeStatic(IVertexBuffer *vbuf) const
{
	const u32 n = vbuf->getCount();
	const bool doNormals = hasNormalDeltas();
	for (u32 v = 0; v < n; ++v) {
		core::vector3df pos, nrm;
		computeMorphedVertex(v, baseWeights, doNormals,
				vbuf->getPosition(v), vbuf->getNormal(v), pos, nrm);
		vbuf->getPosition(v) = pos;
		if (doNormals)
			vbuf->getNormal(v) = nrm;
	}
	vbuf->setDirty();
}

} // end namespace scene
